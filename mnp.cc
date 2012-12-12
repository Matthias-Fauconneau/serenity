#include "process.h"
#include "string.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "text.h"

struct Simulation : Widget {
    // Affichage de la simulation
    Window window __(this, int2(0,1050), "Simulation"_);

    // Paramètres de la simulation pour l'Argon
    const int Nd = 20; // Nombre de particules selon une dimension
    const int N = Nd*Nd; // Nombre de particules
    // Toutes les grandeurs ont été multiplité par 10^10 pour éviter le soupassement (underflow)
    const float s = 3.4; //Å Position du 1er zero du potentiel de Lennard-Jones
    const float e = 1.65e-11; //e-10 J Profondeur du puit de potentiel (à rm=2^(1/6)s)
    const float m = 6.69e-16; //e-10 kg Masse d'une particule

    // Paramètres dérivées
    const int T = 100; // Nombre de pas entre chaque affichages
    const float dt = s*sqrt(m/e)/1000;
    const float rm = pow(2,1./6)*s; // Distance minimisant le potentiel
    const float r0 = rm; // Distance initial entre deux particules (chaque particule est au zéro du potentiel de ses 2 premiers voisins)
    const float L = (Nd-1)*r0; // Taille total du système
    const float rc = 2.5*s; //pour rc=2.5s, le potentiel vaut -0.02e
    const int n = rc/r0; // min n tq n·r0 > rc →  n = floor(rc/r0)

    // Alloue deux tableaux de positions (Verlet nécessite uniquement les deux dernières positions)
    Buffer<vec2> X[2] = {Buffer<vec2>(N,N),Buffer<vec2>(N,N)};
    int t=0;

    // Initialisation
    Simulation() {
        // Configuration de l'application
        window.localShortcut(Escape).connect(&exit);
        window.backgroundCenter=window.backgroundColor=1;

        // Rappel des paramètres
        log("σ:",s,"ε:",e,"m:",m,"dt:",dt,"r0:",r0,"L:",L,"rc:",rc,"n:",n);

        // Initialise les positions de façon homogène entre -L/2 et L/2 avec une vitesse nulle
        for(int i: range(N)) X[0][i]=X[1][i] = vec2(-L/2) + vec2((i%Nd)*r0,(i/Nd)*r0);
    }

    uint64 frameEnd=cpuTime(), frameTime=5000; // last frame end and initial frame time estimation in microseconds

    // Appelée par window à chaque affichage
    void render(int2 position, int2 size) override {
        float Ec=0;
        for(unused int pas: range(T)) { // Calcule T pas entre chaque affichage
            int t0 = t%2, t1 = !t0; // Alterne entre les deux tableaux de positions
            parallel<8>(N, [&](uint i) { // Calcul parallèle sur 8 threads (4 cores × 2 hyperthreads/core)
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
                    //if(r>3*rc) continue; // Compromis entre doublage de performance et éjection artificiel d'atome
                    float sr6 = powi(s/r, 6);
                    float sr12 = sr6*sr6; // Le potentiel de Lennard-Jones utilise l'exposant 12=2·6 pour permettre cette optimisation
                    float fji = //Force exercée par j sur i
                            4*e/r * (
                                - 12*sr12 // Terme de repulsion de Pauli (des nuages électroniques)
                                + 6*sr6 // Terme d'attraction de Van der Waals (interactions dipolaires)
                                );
                    fi += (rij/r)*fji; // Force portée par le vecteur rij
                }
                // Integration de la position par l'algorithme de Verlet
                vec2 &x0 = X[t0][i], &x1 = X[t1][i];
                vec2 a = fi/m;
                x1 = 2.f*x0 - x1 + dt*dt*a; // x[t+1] remplace x[t-1]
                float v = length(x1-x0);
                Ec += 1./2 * m * v*v;
            });
            t++;
        }
        Ec /= T; //Moyenne Ec sur les derniers pas

        // Affichage de la simulation toutes les picosecondes
        float max = 60;
#if 0 //Il vaut mieux perdre de vue les atomes qui sont parfois éjectées à 2D
        for(vec2 x: X[0]) {
            max= ::max(max, abs(x.x));
            max= ::max(max, abs(x.y));
        }
#endif
        for(vec2 x: X[0]) {
            disk(vec2(position)+vec2(size)*vec2((1+x.x/max)/2,(1-x.y/max)/2), size.x*rm/max/2/2);
        }

        uint frameEnd = cpuTime(); frameTime = ( (frameEnd-this->frameEnd) + (16-1)*frameTime)/16; this->frameEnd=frameEnd;

        string etat = str("N",N,"pas",t,"t",t*dt*1e-1,"ns Ec",Ec*1e-10,"J",1000000/frameTime,"fps");
        Text(etat).render(position);
        if(t%(T*10)==0) log(etat);

        window.render(); // Anime la simulation à la vitesse d'affichage
    }
} simulation;
