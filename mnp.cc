#include "process.h"
#include "string.h"
#include "time.h"
#include "window.h"
#include "display.h"
#include "text.h"

struct Simulation : Widget {
    // Affichage de la simulation
    Window window __(this, int2(0,1000), "Simulation"_);

    // Paramètres de la simulation pour l'Argon
    const int N = 100; // Nombre de particules
    // Toutes les grandeurs ont été multiplité par 10^10 pour éviter le soupassement (underflow)
    const float s = 3.4; //Å Position du 1er zero du potentiel de Lennard-Jones
    const float e = 1.65e-11; //e-10 J Profondeur du puit de potentiel (à rm=2^(1/6)s)
    const float m = 6.69e-16; //e-10 kg Masse d'une particule

    // Paramètres dérivées
    const float dt = s*sqrt(m/e)/100;
    const float rm = pow(2,1./6)*s; // Distance minimisant le potentiel
    const float r0 = s; // Distance initial entre deux particules (chaque particule est au zéro du potentiel de ses 2 premiers voisins)
    const float L = (N-1)*r0; // Taille total du système
    const float rc = 2.5*s; //pour rc=2.5s, le potentiel vaut -0.02e
    const int n = rc/r0; // min n tq n·r0 > rc →  n = floor(rc/r0)

    // Alloue deux tableaux de positions (Verlet nécessite uniquement les deux dernières positions)
    Buffer<float> X[2] = {Buffer<float>(N,N),Buffer<float>(N,N)};
    int t=0;

    // Initialisation
    Simulation() {
        // Configuration de l'application
        window.localShortcut(Escape).connect(&exit);
        window.backgroundCenter=window.backgroundColor=1;

        // Rappel des paramètres
        log("σ:",s,"ε:",e,"m:",m,"dt:",dt,"r0:",r0,"L:",L,"rc:",rc,"n:",n);

        // Initialise les positions de façon homogène entre -L/2 et L/2 avec une vitesse nulle
        for(int i: range(N)) X[0][i]=X[1][i] = -L/2 + i*r0;
    }

    uint64 frameEnd=cpuTime(), frameTime=5000; // last frame end and initial frame time estimation in microseconds

    // Appelée par window à chaque affichage
    void render(int2 position, int2 size) override {
        float Ec=0;
        for(unused int pas: range(100)) { // Simulation du système
            int t0 = t%2, t1 = !t0; // Alterne entre les deux tableaux de positions
            for(int i: range(N)) {
                // Calcul de la force total exercée sur la particule i par ses voisins
                float fi=0;
                for(int j: range(max(0,i-n),min(i+n+1,N))) {
                    if(i==j) continue;
                    float r = X[t0][j]-X[t0][i];
                    float sr6 = powi(s/r, 6);
                    float sr12 = sr6*sr6; // Le potentiel de Lennard-Jones utilise l'exposant 12=2·6 pour permettre cette optimisation
                    float fji = //Force exercée par j sur i
                            4*e/r * (
                                - 12*sr12 // Terme de repulsion de Pauli (des nuages électroniques)
                                + 6*sr6 // Terme d'attraction de Van der Waals (interactions dipolaires)
                                );
                    fi += fji; //Force exercée par un voisin de gauche
                }
                // Integration de la position par l'algorithme de Verlet
                float &x0 = X[t0][i], &x1 = X[t1][i];
                float a = fi/m;
                x1 = 2*x0 - x1 + dt*dt*a; // x[t+1] remplace x[t-1]
                float v = x1-x0;
                Ec += 1./2 * m * v*v;
            }
            t++;
        }
        Ec /= 100; //Moyenne Ec sur les 100 derniers pas

        // Affichage de la simulation toutes les picosecondes
        float max=0;
        for(float x: X[0]) {
            max= ::max(max, abs(x));
        }
        for(float x: X[0]) {
            float y = 1./2; //à faire: simulation à 2 dimensions
            disk(vec2(position)+vec2(size)*vec2((1+x/max)/2,y), size.x*rm/max/2/2);
        }

        uint frameEnd = cpuTime(); frameTime = ( (frameEnd-this->frameEnd) + (16-1)*frameTime)/16; this->frameEnd=frameEnd;

        string etat = str(t,"t",t*dt*1e-1,"ns Ec",Ec*1e-10,"J",1000000/frameTime,"fps");
        Text(etat).render(position);
        log(etat);

        window.render(); // Anime la simulation à la vitesse d'affichage
    }
} simulation;
