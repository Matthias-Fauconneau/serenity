#include "process.h"
#include "string.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "text.h"
#include "resample.h"
#include "asound.h"
#include "record.h"

struct Simulation : Widget {
    // Paramètres de la simulation pour l'Argon
    const int Nd = 10; // Nombre de particules selon une dimension
    const int N = Nd*Nd; // Nombre de particules
    // Toutes les grandeurs ont été multiplité par 10^10 pour éviter le soupassement (underflow)
    const float s = 3.4; //Å Position du 1er zero du potentiel de Lennard-Jones
    const float e = 1.65e-11; //e-10 J Profondeur du puit de potentiel (à rm=2^(1/6)s)
    const float m = 6.69e-16; //e-10 kg Masse d'une particule

    // Paramètres dérivées
    const float dt = s*sqrt(m/e)/1000;
    const float rm = pow(2,1./6)*s; // Distance minimisant le potentiel
    const float r0 = rm; // Distance initial entre deux particules (chaque particule est au zéro du potentiel de ses 2 premiers voisins)
    const float L = (Nd-1)*r0; // Taille total du système
    const float rc = 2.5*s; //pour rc=2.5s, le potentiel vaut -0.02e
    const int n = rc/r0; // min n tq n·r0 > rc →  n = floor(rc/r0)

    // Alloue deux tableaux de positions (Verlet nécessite uniquement les deux dernières positions)
    Buffer<vec2> X[2] = {Buffer<vec2>(N,N),Buffer<vec2>(N,N)};
    int t=0; // Temps en pas de dt
    float Ep=0; // Energie potentiel total du système
    float Ec=0; // Energie cinétique total du système
    float Ei=0; // Energie mécanique initial
    Lock statusLock;

    // Thread d'execution de la simulation (dirigé par la sortie audio, séparé pour éviter que l'affichage fasse rater une période audio)
    Thread thread;
    // Affichage de la simulation
    Window window __(this, int2(0,720)/*int2(1280,720)*/, "Simulation"_);
    Timer timer;
    // Bruit de la simulation
    AudioOutput audio __({this,&Simulation::synthesize},thread,true);
    Resampler resampler __(audio.channels, audio.rate/128, audio.rate, audio.periodSize); // Réduit la vitesse à 44000/128 pas/seconde
    // Enregistrement video+audio
    array< Buffer<float> > audioQueue; Lock audioQueueLock; // Queue des frames audio à enregistrer (par le thread principal)
    Record record;

    // Initialisation
    Simulation() {
        // Configuration de l'application
        window.localShortcut(Escape).connect(&exit);
        window.backgroundCenter=window.backgroundColor=1;
        window.localShortcut(Key('R')).connect([this]{record.start("Simulation"_);});
        timer.timeout.connect(&window,&Window::render);

        // Initialise les positions de façon homogène entre -L/2 et L/2 avec une vitesse nulle
        for(int i: range(N)) X[0][i]=X[1][i] = vec2(-L/2) + vec2((i%Nd)*r0,(i/Nd)*r0);

        // Démarre la simulation
        audio.start();
        thread.spawn();
    }

    // Appelée par window à chaque affichage de la simulation
    void render(int2 position, int2 size) override {
        // Représente les atomes par des disques noirs
        float taille=min(size.x,size.y);
        for(vec2 x: X[0]) {
            vec2 center((size.x-taille)/2,(size.y-taille)/2);
            float l = Nd*r0/2*3/2;
            disk(vec2(position)+center+taille*vec2((1+x.x/l)/2,(1-x.y/l)/2), taille*rm/l/2/2);
        }
        {// Affiche le status actuel de la simulation
            Locker lock(statusLock);
            Text text(str(t*dt*1e-1,"ns",
                     "Ep",Ep*1e-10,"J",
                     "Ec",Ec*1e-10,"J",
                     "Em",(Ep+Ec)*1e-10,"J",
                     "Ei",(Ei)*1e-10,"J",
                     "ΔE/E",(Ep+Ec-Ei)/Ei));
            text.layout();
            fill(position+Rect(text.textSize),white);
            text.render(position);
        }

        // Enregistrement video+audio
        if(record && audioQueue) { // ~ 3 périodes audio / image ((44100/512)/30)
            Locker audioQueueLocker(audioQueueLock); // array n'est pas thread-safe par default :/
            Buffer<float> audio = audioQueue.take(0);
            record.capture(audio,audio.size/2);
        }

        if(t*dt>=1) exit(); //Quitte automatiquement aprés 0.1 ns de simulation
        timer.setAbsolute((realTime()+17)/1000,(realTime()+17)%1000*1000000); // Limite l'affichage à 60fps
    }

    // Appelée par audio à chaque frame
    bool synthesize(int32* output, uint audioSize) {
        uint T = resampler.need(audioSize);
        float signal[2*T];

        float Ep=0, Ec = 0;
        for(int ti: range(T)) { // Calcule T pas de temps
            int t0 = t%2, t1 = !t0; // Alterne entre les deux tableaux de positions
            float sample=0;
            parallel</*8*/4>(N, [&](uint i) { // Calcul parallèle sur 8 threads (4 cores × 2 hyperthreads/core) (ou pas -> plus silencieux)
                // Calcul de la force total exercée sur la particule i par ses voisins
                vec2 fi=0;
#if UNE_DIMENSION
                for(uint j: range(max(0,i-n),min(i+n+1,N))) { // Seulement si les particules ne peuvent pas se croiser (i.e 1D)
#else
                for(uint j: range(N)) { // O(N^2)! Un vrai programme partionnerai l'espace
#endif
                    if(i==j) continue;
                    vec2 rij = X[t0][j]-X[t0][i];
                    float r = length(rij);
                    //if(r>3*rc) continue; // Néglige la faible attraction longue distance (évite uniquement un peu d'arithmétique)

                    float sr6 = powi(s/r, 6);
                    float sr12 = sr6*sr6; // Le potentiel de Lennard-Jones utilise l'exposant 12=2·6 pour permettre cette optimisation

                    float uji = 4*e * ( // Potentiel d'interaction de Lennard-Jones entre i et j
                            +sr12 // Terme de repulsion de Pauli (des nuages électroniques)
                            -sr6 ); // Terme d'attraction de Van der Waals (interactions dipolaires)
                    Ep += uji; // Calcul de l'energie potentiel total d'interaction pour vérifier la conservation de l'énergie mécanique

                    float fji = 4*e/r * ( -12*sr12 +6*sr6 ); //Force exercée par j sur i (-∇u)
                    fi += fji*(rij/r); // Force portée par le vecteur rij
                }
                // Integration de la position par l'algorithme de Verlet
                vec2 &x0 = X[t0][i], &x1 = X[t1][i];
                vec2 a = fi/m;
                x1 = 2.f*x0 - x1 + dt*dt*a; // x[t+1] remplace x[t-1]
                float v = length(x1-x0);
                sample += (x1-x0).x; // Bruit des atomes
                Ec += 1./2 * m * v*v;
            });
            t++;
            static float max=1e-2; // Normalise l'amplitude du bruit
            max=::max(max, sample);
            signal[2*ti+0]=signal[2*ti+1]= (2*sample/max-1);
        }
        Ep /= 2; // Corrige la double accumulation du potentiel (2x/pair)
        Ep /= T; // Moyenne de Ep sur T pas
        Ec /= T; // Moyenne de Ec sur T pas
        {
            Locker lock(statusLock);
            this->Ec = Ec, this->Ep = Ep; // Pour l'affichage du status
            if(!Ei && t*dt>=0.01) Ei = Ep+Ec; // Energie mécanique "initial" (calculée aprés 0.001 ns)
        }

        Buffer<float> audio(2*audioSize,2*audioSize);
        resampler.filter(signal,T,audio,audioSize);
        for(uint i: range(2*audioSize)) audio[i]*=0x1p29f;
        for(uint i: range(2*audioSize)) output[i]=audio[i]; // Sortie son
        if(record) {
            Locker audioQueueLocker(audioQueueLock); // array n'est pas thread-safe par default :/
            audioQueue << move(audio);
        }
        return true;
    }
} simulation;
