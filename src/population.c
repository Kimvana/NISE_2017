#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <fftw3.h>
#include "omp.h"
#include "types.h"
#include "NISE_subs.h"
#include "population.h"

void population(t_non *non){
  // Initialize variables
  float avall,flucall;
  float *Hamil_i_e,*H,*e,*Hamil_av;

  // Aid arrays
  float *vecr,*veci,*vecr_old,*veci_old;
  float *Pop,*PopF;

  /* Floats */
  float shift1;
  float x;
  float pr,pi;

  /* File handles */
  FILE *H_traj;
  FILE *outone,*log;

  /* Integers */
  int nn2,N;
  int itime,N_samples;
  int samples;
  int ti,tj,i,j;
  int t1,fft;
  int elements;
  int Nsam;
  int counts;
  int a,b,c,d;

  /* Time parameters */
  time_t time_now,time_old,time_0;
  /* Initialize time */
  time(&time_now);
  time(&time_0);
  shift1=(non->max1+non->min1)/2;
  printf("Frequency shift %f.\n",shift1);
  non->shifte=shift1;

  // Allocate memory
  N=non->singles;
  nn2=non->singles*(non->singles+1)/2;
  Hamil_i_e=(float *)calloc(nn2,sizeof(float));
  Hamil_av=(float *)calloc(nn2,sizeof(float));
  H=(float *)calloc(N*N,sizeof(float));
  e=(float *)calloc(N,sizeof(float));
  Pop=(float *)calloc(non->tmax,sizeof(float));
  PopF=(float *)calloc(non->tmax*non->singles*non->singles,sizeof(float));

  /* Open Trajectory files */
  H_traj=fopen(non->energyFName,"rb");
  if (H_traj==NULL){
    printf("Hamiltonian file not found!\n");
    exit(1);
  }

  itime=0;
  // Do calculation
  N_samples=(non->length-non->tmax1-1)/non->sample+1;
  if (N_samples>0) {
    printf("Making %d samples!\n",N_samples);
  } else {
    printf("Insufficient data to calculate spectrum.\n");
    printf("Please, lower max times or provide longer\n");
    printf("trajectory.\n");
    exit(1);
  }

  if (non->end==0) non->end=N_samples;
  if (non->end>N_samples){
    printf(RED "Endpoint larger than number of samples was specified.\n" RESET);
    printf(RED "Endpoint was %d but cannot be larger than %d.\n" RESET,non->end,N_samples);
    exit(0);
  }
  Nsam=non->end-non->begin;

  log=fopen("NISE.log","a");
  fprintf(log,"Begin sample: %d, End sample: %d.\n",non->begin,non->end);
  fclose(log);

  if (!strcmp(non->basis,"Local")){
    printf("Using the local (site) basis for population transfer.\n");
    printf("The transfer rates are between sites.\n");
  } else if (!strcmp(non->basis,"Adiabatic")) {
    printf("Using the adiabatic eigen basis for population transfer.\n");
    printf("The transfer rates are between eigenstates.\n");
  } else if (!strcmp(non->basis,"Average")) {
    printf("Using the average eigen basis for population transfer.\n");
    printf("The transfer rates are between eigenstates.\n");
  }

  if (non->propagation==1){
    printf("==============================================================\n");
    printf(RED "You cannot use the Coupling propagation scheme for Popultation\n");
    printf("calculations! Please, use the 'Propagation Sparse' option.\n");
    printf("Use 'Threshold 0.0' for highest accuracy.\n");
    printf("Aborting run now.\n" RESET);
    printf("==============================================================\n");
    exit(0);
  }

  // Find average basis
  if (!strcmp(non->basis,"Average")){
    for (samples=non->begin;samples<non->end;samples++){
      ti=samples*non->sample;
      /* Read Hamiltonian */
      if (read_He(non,Hamil_i_e,H_traj,ti)!=1){
        printf("Hamiltonian trajectory file to short, could not fill buffer!!!\n");
        exit(1);
      }
      for (a=0;a<nn2;a++){
        Hamil_av[a]+=Hamil_i_e[a];
      }
    }
    for (a=0;a<nn2;a++){
      Hamil_av[a]=Hamil_av[a]/(samples-non->begin);
    }
    /* Diagonalize average Hamiltonian */
    build_diag_H(Hamil_av,H,e,non->singles);
  }

  /* Loop over samples */
  for (samples=non->begin;samples<non->end;samples++){
    vecr=(float *)calloc(non->singles*non->singles,sizeof(float));
    veci=(float *)calloc(non->singles*non->singles,sizeof(float));
    /* Initialize */
      for (a=0;a<non->singles;a++) vecr[a+a*non->singles]=1.0;

    ti=samples*non->sample;      
    for (t1=0;t1<non->tmax;t1++){
      tj=ti+t1;
      /* Read Hamiltonian */
      if (read_He(non,Hamil_i_e,H_traj,tj)!=1){
        printf("Hamiltonian trajectory file to short, could not fill buffer!!!\n");
        exit(1);
      }
      /* Calculate population evolution */
      if (!strcmp(non->basis,"Local")){ /* Local basis */
        for (a=0;a<non->singles;a++){
          Pop[t1]+=vecr[a+a*non->singles]*vecr[a+a*non->singles];
          Pop[t1]+=veci[a+a*non->singles]*veci[a+a*non->singles];
        }           
        for (a=0;a<non->singles;a++){
          for (b=0;b<non->singles;b++){
            PopF[t1+(non->singles*b+a)*non->tmax]+=vecr[a+b*non->singles]*vecr[a+b*non->singles];
            PopF[t1+(non->singles*b+a)*non->tmax]+=veci[a+b*non->singles]*veci[a+b*non->singles];
          }
        }
      } else if (!strcmp(non->basis,"Adiabatic")) { /* Adiabatic eigen basis */
        /* Read Hamiltonian */
        if (read_He(non,Hamil_i_e,H_traj,ti+t1)!=1){
          printf("Hamiltonian trajectory file to short, could not fill buffer!!!\n");
          exit(1);
        }
        build_diag_H(Hamil_i_e,H,e,non->singles);
        /* Loop over final/initial adabatic states */
        for (a=0;a<non->singles;a++){
          pr=0;
          pi=0;
          /* Loop over sites */
          for (b=0;b<non->singles;b++){
            /* Loop over sites */
            for (c=0;c<non->singles;c++){
              pr+=H[b+a*non->singles]*vecr[b+c*non->singles]*H[c+a*non->singles];
              pi+=H[b+a*non->singles]*veci[b+c*non->singles]*H[c+a*non->singles];
            }
          }
          Pop[t1]+=pr*pr+pi*pi;
        }
        /* Loop over final and initial states */
        for (a=0;a<non->singles;a++){
          for (d=0;d<non->singles;d++){
            pr=0;
            pi=0;
            for (c=0;c<non->singles;c++){
            /* Loop over sites */
              for (b=0;b<non->singles;b++){
                pr+=H[b+a*non->singles]*vecr[b+c*non->singles]*H[c+d*non->singles];
                pi+=H[b+a*non->singles]*veci[b+c*non->singles]*H[c+d*non->singles];
              }
            }
            PopF[t1+(non->singles*d+a)*non->tmax]+=pr*pr+pi*pi;
          }
        }
      } else if (!strcmp(non->basis,"Average")) { /* Average eigen basis */
      /* Loop over final/initial adabatic states */
        for (a=0;a<non->singles;a++){
          pr=0;
          pi=0;
          /* Loop over sites */
          for (b=0;b<non->singles;b++){
            /* Loop over sites */
            for (c=0;c<non->singles;c++){
              pr+=H[b+a*non->singles]*vecr[b+c*non->singles]*H[c+a*non->singles];
              pi+=H[b+a*non->singles]*veci[b+c*non->singles]*H[c+a*non->singles];
            }
          }
          Pop[t1]+=pr*pr+pi*pi;
        }
        /* Loop over final and initial states */
        for (a=0;a<non->singles;a++){
          for (d=0;d<non->singles;d++){
            pr=0;
            pi=0;
            for (c=0;c<non->singles;c++){
            /* Loop over sites */
              for (b=0;b<non->singles;b++){
                pr+=H[b+a*non->singles]*vecr[b+c*non->singles]*H[c+d*non->singles];
                pi+=H[b+a*non->singles]*veci[b+c*non->singles]*H[c+d*non->singles];
              }
            }
            PopF[t1+(non->singles*d+a)*non->tmax]+=pr*pr+pi*pi;
          }
        }


      }
      for (a=0;a<non->singles;a++){
        /* Probagate vector */
        if (non->thres==0 || non->thres>1){
          propagate_vec_DIA(non,Hamil_i_e,vecr+a*non->singles,veci+a*non->singles,1);
        } else {
          elements=propagate_vec_DIA_S(non,Hamil_i_e,vecr+a*non->singles,veci+a*non->singles,1);
          if (samples==non->begin && a==0){
            if (t1==0){
//              printf("\n");
//              printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
//              printf("WARNING!!! You are propagating the population with the Coupling scheme!\n");
//              printf("This may lead to large errors and carefull testing to the Trotter setting\n");
//              printf("must be performed. The Coupling scheme is often only reliable for short times.\n");
//              printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n\n");
              printf("Sparce matrix efficiency: %f pct.\n",(1-(1.0*elements/(non->singles*non->singles)))*100);
              printf("Pressent tuncation %f.\n",non->thres/(non->deltat*icm2ifs*twoPi/non->ts)*(non->deltat*icm2ifs*twoPi/non->ts));
              printf("Suggested truncation %f.\n",0.001);
            }
          }
        }
      }
    }

  }
  /* Correct for when not starting at sample zero */
  samples=samples-non->begin;
  /* Write populations */
  outone=fopen("Pop.dat","w");
  if (outone==NULL){
    printf("Problem encountered opening Pop.dat for writing.\n");
    printf("Disk full or write protected?\n");
    exit(1);
  }
  for (t1=0;t1<non->tmax1;t1+=non->dt1){
    fprintf(outone,"%f %e\n",t1*non->deltat,Pop[t1]/samples/non->singles);
  }
  fclose(outone);
  outone=fopen("PopF.dat","w");
  if (outone==NULL){
    printf("Problem encountered opening PopF.dat for writing.\n");
    printf("Disk full or write protected?\n");
    exit(1);
  }
  for (t1=0;t1<non->tmax1;t1+=non->dt1){
    fprintf(outone,"%f ",t1*non->deltat);
    for (a=0;a<non->singles;a++){
      for (b=0;b<non->singles;b++){
        fprintf(outone,"%e ",PopF[t1+(non->singles*b+a)*non->tmax]/samples);
      }
    }
    fprintf(outone,"\n");
  }
  fclose(outone);

  free(Hamil_i_e);
  free(Hamil_av);
  free(H);
  free(e);
  free(Pop);
  free(PopF);
  // The calculation is finished, lets write output
  log=fopen("NISE.log","a");
  fprintf(log,"Finished Calculating Population Transfer!\n");
  fprintf(log,"Writing to file!\n");  
  fclose(log);

  fclose(H_traj);
 

  printf("----------------------------------------------\n");
  printf(" Population calculation succesfully completed\n");
  printf("----------------------------------------------\n\n");

  return;
}	

