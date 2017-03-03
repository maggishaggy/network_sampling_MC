#include "pro_classes.h"
#include "rand_gsl.h"
#include "constrainParms.h"
#include "metrics_class.h"
#include "md_timer.h"
#include "entropy_fits.h"
#include "modify_network_and_conc.h"
#include "calc_ratios.h"
#include "mc_fits.h"
#include "write_ppis.h"
#include "add_iface_to_ppi.h"
#include "accept_iin_moves.h"
#include "write_outs.h"
#include "get_total_pro_conc.h"
#include "opt_constrained_complex_qp.h"
#include "matrix_solve_complex.h"
#include "build_Amatrix.h"
#include "net_props.h"
#include "matmultiply.h"
#include "network_metrics2.h"
#include "findAlpha.h"

#include <cstdlib>

  struct MD_Timer looptime2;

using namespace std;

void modify_network_only(int nwhole, int ninterface, int *numpartners, int **Speclist, Protein *wholep, ppidata *ppi, int *p_home, constrainParms &plist,  ofstream &globalfile, int **ehome, int **Epred, int **templist, int *tmppartners, Protein *wholetemp, int * nbor, int *Adj, ofstream &idfile, ofstream &matchfile)
{
  /*This function will mutate the IIN each iteration, and accept each move based on a Boltzmann probability derived from the change in fitness
   */



  //Calculate maximum # of interfaces
  int i, j;
  int rr;
  /*  int maxni=0;
  for(i=0;i<nwhole;i++){
    for(j=0;j<ppi[i].nppartner;j++){
      maxni++;
      if(ppi[i].pplist[j]==i)
	maxni++;
    }

    }*/

  //Declare variables
  
  int PPIedge=plist.Nedge;
  int maxnif = PPIedge*2;
  int *Adj2=new int[maxnif*maxnif];  
  Protein *wholepOpt = new Protein[nwhole];
  int *numpartnersOpt = new int[maxnif];
  int **SpeclistOpt = new int*[maxnif];
  for(i=0;i<maxnif;i++)
    SpeclistOpt[i]=new int[MAXP];
  int iterOpt=0;
  double numedge;
  int output_count=0;
  double nsquares_mean=0;
  double nchains_mean=0;
  double n4hubs_mean=0;
  double n4mers_mean=0;
  double numedge_mean=0;
  double ninterface_mean=0;
  int *ifacepresent= new int[maxnif];
  int **modlist = new int*[maxnif];
  for(i=0;i<maxnif;i++)
    modlist[i]=new int[maxnif];
  int modcurr;
  int *modsizes = new int[maxnif];
  int maxne=int(1.5*PPIedge);
  cout << "maximum number of edges is " << maxne << endl;
  double degk_mean=0;
  double degalpha_mean=0;
  double Clocal_mean=0;
  double Cglobal_mean=0;
  double giant_mean=0;
  int giant;
  double Cglobal, Clocal, degalpha;
  double degk=0.0;
  int open,closed;
  double avg, selfavg;
  int *self=new int[maxnif];
  double *pmf = new double[maxnif];
  ifstream pmf_file("PMFfile.txt");
  int pmflines = 11*(maxnif - nwhole + 1) * (maxne - PPIedge + 1);
  double **sampPMFs = new double*[pmflines];
  double omega = plist.omega;
  double kappa = plist.kappa;
  double mu = plist.mu;
  double beta=plist.beta;
  double zeta=plist.zeta;
  int nsquares, nchains, n4hubs, n4mers;
  int PAEflag = plist.PAEflag;
  int fitnessflag = plist.fitnessflag;
    //End variables
  
//Read in pmf file
  if(PAEflag==1)
    readPMFfile(pmf_file, sampPMFs, pmflines);

  //Create output file
  char fname[300];
  sprintf(fname,"OptNet_beta%g.mu%g.kappa%g.omega%g.zeta%g.Temp%g.fs%d.out", beta, mu, kappa, omega, zeta, plist.MCtemp, plist.flagsplit);
  globalfile.open(fname,std::ofstream::out | std::ofstream::trunc);
  globalfile <<"Iter\tNinterfaces\tNedges\tNsquares\tLargest Island Size\tAlpha(deg dist)\tAverage Degree\tCLocal\tCGlobal\tNchains\tNFour node hubs\tTotal fourmers\n";

  
  Metrics mets;
  Metrics rndmets;

  int *selfflag=new int[nwhole];

  for(i=0;i<nwhole;i++){
    selfflag[i]=0;//no self binding
    for(j=0;j<ppi[i].nppartner;j++){
      if(ppi[i].pplist[j]==i)
	selfflag[i]=1;
    }
  }
  for(i=0;i<nwhole;i++){
    wholetemp[i].ninterface=wholep[i].ninterface;
    for(j=0;j<wholep[i].ninterface;j++)
      wholetemp[i].valiface[j]=wholep[i].valiface[j];
  }

  int t=0;
  int cswap=10;
  //MC steps
  double newfit;
  double prob, del;
  double tol=+1E-12;
  int flagacc;

  double globopt;
  
  
  double oldfit;
  double network_fit;
  

  int Nif = ninterface;

    
  double **fourmer = new double*[2]; //Obsolete variable that we still need to define to avoid breaking existing code.
    
  for(i=0;i<2;i++)
    fourmer[i]=new double[4];
    
  double **hist = new double*[7];
  for(j=0;j<7;j++){
    hist[j]=new double[2];
    hist[j][0]=0;
    hist[j][1]=0;
  }
    
  double *histfour = new double[6];
  int *typelist = new int[2]; //Also an obsolete variable
    
  for(i=0;i<maxnif*maxnif;i++)
    Adj2[i]=0;
  for(i=0;i<ninterface;i++){
    for(j=0;j<numpartners[i];j++){
      Adj2[i*ninterface + Speclist[i][j]]=1;
    }
    Adj2[i*ninterface+i]=1;
  }

  //Calculate initial number of tetramers
  four_motif(Nif,Adj2,fourmer,histfour,hist,typelist);
  nsquares=hist[4][1];
  nchains=hist[3][1];
  n4hubs=hist[3][0];
  n4mers=hist[3][1]+hist[3][0]+hist[4][0]+hist[4][1]+hist[5][0]+hist[6][0];        
    
  //Find starting number of IIN edges
  numedge=0.0;
  for(i=0;i<ninterface;i++){
    for(j=0;j<numpartners[i];j++){
      if(Speclist[i][j]==i)
	numedge+=1;
      else
	numedge+=0.5;
    }
  }

  cout<<"Starting number of edges is: " << numedge << endl;
  double edgediff=omega*(numedge-PPIedge);//Used in fitness function

  //Now find local grid coefficients and clustering coefficients for the starting network.
  double *grid_cofs = new double[maxnif];
  double *clocals = new double[maxnif];
  for(i=0;i<maxnif;i++){
    grid_cofs[i]=0;
    clocals[i]=0;
  }
  for(i=0;i<ninterface;i++){
    clocals[i]=cluster_cof_mod(i,ninterface,numpartners,Speclist);
  }
  grid_cof(grid_cofs,ninterface,numpartners,Speclist);
  
  //Calculate fitness of starting network
  //Which fitness function is used will depend on "fitness flag" parameter
  if(fitnessflag==0) 
    network_fit=mc_fit4(ninterface, grid_cofs, clocals, kappa, beta, nwhole, wholep, mu, numpartners, edgediff);
  else if(fitnessflag==1) //Penalize absolute number of interfaces
    network_fit=mc_fit5(ninterface, grid_cofs, clocals, kappa, beta, nwhole, wholep, mu, numpartners, edgediff);
  else //Penalize absolute number of interfaces & self loops on hubs
    network_fit=mc_fit6(ninterface, grid_cofs, clocals, kappa, beta, nwhole, wholep, mu, numpartners, edgediff, Speclist, zeta);
    
  cout <<"network fit: "<<network_fit<<endl;

  //Find and write parameters of starting network
  cluster_cof_self(ninterface, open, closed, avg, Speclist, numpartners, self, selfavg);
  Cglobal=closed*1.0/(1.0*(open+closed));
  if(closed==0)Cglobal=0;
  Clocal=avg/ninterface;
  //cout<<"Clocal: " << Clocal << " Cglobal: "<<Cglobal << endl;
  for(j=0;j<maxnif;j++)
    pmf[j]=0.0;
  for(j=0;j<ninterface;j++){
    degk+=numpartners[j];
    pmf[numpartners[j]-1]++;
  }
  degk/=(1.0*ninterface);
  for(j=0;j<maxnif;j++){
    pmf[j]/=(1.0*ninterface);
  }
  if(PAEflag==1)
    degalpha=findAlpha(pmf, numedge, ninterface, nwhole,maxnif, PPIedge, maxne, sampPMFs);
  else if(PAEflag==0)
    degalpha=findAlpha2(pmf,numedge,ninterface);
  else{
    degalpha=0;
    for(j=0;j<ninterface;j++){
      degalpha+=pow(numpartners[j]-degk,2);
    }
    degalpha/=ninterface;//This is the variance
    degalpha/=degk;//This is the Index of Dispersion
  }
  modcurr = find_islands(ninterface, Adj2, modlist, modsizes);
  //Find largest module size
  giant=0;
  for(i=0;i<modcurr;i++){
    if(modsizes[i]>giant)
      giant=modsizes[i];
  }
  //Write parameters to output file
  globalfile << 0 << "\t\t" << ninterface << "\t\t" << numedge<<"\t\t"<<nsquares << "\t\t" << giant << "\t\t" << degalpha << "\t\t" << degk << "\t\t" << Clocal << "\t\t" << Cglobal << "\t\t" << nchains << "\t\t" << n4hubs << "\t\t" << n4mers << "\t\tFitness: " << network_fit << endl;
  
    

  
  oldfit=network_fit;
  globopt=oldfit;
  cout <<"Original fitness: "<<oldfit<<endl;
  
  double netmet;
  double old_nw_fit=network_fit;
  double MCbeta=1.0/plist.MCtemp;
  double rnum;
  int Nit=plist.Nit_net;

  int iface;
  int n;
  int netwrite=plist.netwrite;

  //Write out starting network
  cout <<"STARTING NETWORK "<<endl;
  write_speclist(ninterface, numpartners, Speclist );
  cout <<"STARTING PPI "<<endl;
  write_ppi(nwhole, wholep);
  double avgratio; 

  double pgen_ratio;//ratio of MC move probabilities, not all are equal

  avgratio=calc_avg_ratio(ninterface, Speclist, numpartners);
  
  

  int acc=0;
  int dec=0;
  int decacc=0;
  int chg=1;
  int neq=0;
  int id, move;
  int Nhold;//Hold the full number of constraints
  int nmove=7;//number of move types
  double moveprob[nmove];//this determines the probability of making each type of move
  int idstart=2; //The first two moves were obsolete. MC simulation selectsion from moves 2-6, five choices in total.
  moveprob[0]=0.2;
  moveprob[1]=0.4;
  moveprob[2]=1.0/5.0;
  moveprob[3]=2.0/5.0;
  moveprob[4]=3.0/5.0;
  moveprob[5]=4.0/5.0;
    moveprob[6]=1.0;
  int moveacc[nmove];
  int moverej[nmove];
  int movetot[nmove];
  
  
  for(i=0;i<nmove;i++){
    moveacc[i]=0;
    movetot[i]=0;
    moverej[i]=0;
  }
  double rmsdtmp;
  double nptmp;
  int ntmpinterface=ninterface;
  double netavg=0.0;
  double netavg2=0.0;
  double fitavg=0.0;
  double fitavg2=0.0;
  double netmin=10000;
  initialize_timer(&looptime2);
  start_timer(&looptime2);
  double pf, pb;
  //Store Speclist in a temporary matrix
  for(i=0;i<ninterface;i++){
    n=numpartners[i];
    tmppartners[i]=n;
    for(j=0;j<n;j++)
      templist[i][j]=Speclist[i][j];
  }
    
  int faul=0;
    
  
  /*BEGIN MC ITERATIONS*/
  for(t=1;t<Nit+1;t++){
    
    //store Speclist in a temporary matrix
    ntmpinterface=ninterface;
    for(i=0;i<ninterface;i++){
      n=numpartners[i];
      tmppartners[i]=n;
      for(j=0;j<n;j++)
      	templist[i][j]=Speclist[i][j];
    }
      
    double *temphistfour=histfour;
    double **temphist=hist;
    int maxnif2=maxnif;
    int maxne2=maxne;
    /*Perform a mutation move, 1 of 5*/
    rnum=1.0*rand_gsl();  
    id=idstart;
    while(rnum>moveprob[id]){
      id++;
      //cout << id << " move prob is: " << moveprob[id] << " rnum is: " << rnum <<  endl; 
      }
    
    move=id;
    //cout << "move is: " << move << endl;
    switch(move){
    case 0:
      //This move is no longer used
      chg=mutate_interfaces_rev(nwhole, ninterface, tmppartners, templist, Adj, wholetemp, ppi, pgen_ratio, selfflag);
      break;
    case 1:
      //This move is no longer used.
      chg=mutate_connections(nwhole, tmppartners, templist, wholep);
      pgen_ratio=1;
      break;
    case 2:
      
      chg=mutate_edge(nwhole, tmppartners, templist, wholep, ppi, pgen_ratio, selfflag, p_home);
      
      break;
    case 3:
      
      chg=combine_interfaces_rev(nwhole, ntmpinterface, tmppartners, templist, Adj, wholetemp, ppi, pgen_ratio, p_home, pf, pb);
      ntmpinterface--;
      
      break;
    case 4:
      
      chg=split_interfaces_rev(nwhole, ntmpinterface, tmppartners, templist, Adj, wholetemp, ppi, pgen_ratio, p_home, maxnif2, selfflag, pf, pb);
      ntmpinterface+=1;
	
      break;

    case 5:
      chg=add_edge(nwhole, ntmpinterface, tmppartners, templist, Adj, wholetemp, ppi, pgen_ratio, p_home, maxnif2, maxne2, selfflag, pf, pb, PPIedge);
      break;    

    case 6:
      chg=delete_edge(nwhole, ntmpinterface, tmppartners, templist, Adj, wholetemp, ppi, pgen_ratio, p_home, maxnif2, selfflag, PPIedge);
      break;
    }
    //cout <<"move type: "<<move<<" chg: "<<chg<<endl;
    /*Once the mutation has been performed, decide whether to accept
      by comparing the fitness of the new network to that of the original
      network
    */

    movetot[move]++;
          
    if(chg!=0){ //Sometimes the move will be un-doable, in which case chg=0 and another move must be chosen

      //Make Adj2 from templist and find current # of edges
      numedge=0.0;
      for(i=0;i<ntmpinterface*ntmpinterface;i++)
	Adj2[i]=0;
      for(i=0;i<ntmpinterface;i++){
	for(j=0;j<tmppartners[i];j++){
	  Adj2[i*ntmpinterface+templist[i][j]]=1;
	  if(templist[i][j]==i)
	    numedge+=1;
	  else
	    numedge+=0.5;
	}
      }

      //Another test: check for ifaces appearing twice
      for(i=0;i<ntmpinterface;i++)
	ifacepresent[i]=0;
      
      for(i=0;i<nwhole;i++){
	for(j=0;j<wholetemp[i].ninterface;j++){
	  ifacepresent[wholetemp[i].valiface[j]]++;
	}
      }
      
      for(i=0;i<ntmpinterface;i++){
	if(ifacepresent[i]>1){
	  cout << "Error: an interface appears more than once. iface " << i << " should be on protein " << p_home[i];
	  exit(0);
	}
      }
      
      four_motif(ntmpinterface,Adj2,fourmer,temphistfour,temphist,typelist);
      for(i=0;i<ntmpinterface;i++){	
	clocals[i]=cluster_cof_mod(i,ntmpinterface,tmppartners,templist);
      }
      grid_cof(grid_cofs,ntmpinterface,tmppartners,templist);

      //Calculate fitness of network after move
      if(fitnessflag==0)
	network_fit=mc_fit4(ntmpinterface, grid_cofs, clocals, kappa, beta, nwhole, wholetemp, mu, tmppartners, omega*(numedge-PPIedge));
      else if(fitnessflag==1)
	network_fit=mc_fit5(ntmpinterface, grid_cofs, clocals, kappa, beta, nwhole, wholetemp, mu, tmppartners, omega*(numedge-PPIedge));
      else
	network_fit=mc_fit6(ntmpinterface, grid_cofs, clocals, kappa, beta, nwhole, wholetemp, mu, tmppartners, omega*(numedge-PPIedge),templist,zeta);
    

      //cout<<"Network fitness is: " << network_fit << endl;
    
      newfit=network_fit;
      /*If we accept, then copy templist into Speclist, otherwise, leave
      templist alone*/
      del=newfit-oldfit;//want small fitness
      //cout << "Del equals: " << del << endl;
      //Calculate probability of accepting move
      prob=exp(-MCbeta*del)*pgen_ratio;
    
      //cout <<"in prob pgen: "<<pgen_ratio<<" del: "<<del<<" prob: "<<prob<<endl;
      if(prob>1)
	flagacc=1;
      else{
		
	rnum=1.0*rand_gsl();
	// cout << "rnum is: " << rnum << endl;
      
	dec++;
	if(rnum<prob){
	  flagacc=1;
	
	  decacc++;
	} else
	  flagacc=0;
      }
   
      if(flagacc==1){

	//Accept the move. Change Speclist to templist.
	acc++;
	//cout <<"Accepted move of type "<<move<<" change: "<<del<<" newfit: "<<newfit<<" oldfit: "<<oldfit<<endl;
	nsquares=temphist[4][1];
	nchains=temphist[3][1];
	n4hubs=temphist[3][0];
	n4mers=temphist[3][1]+temphist[3][0]+temphist[4][0]+temphist[4][1]+temphist[5][0]+temphist[6][0];    
      
	moveacc[move]++;
    
	switch(move){
	case 0:
	  accept_swap_iface(newfit, oldfit, ninterface, numpartners, Speclist, tmppartners, templist, nwhole, wholep, wholetemp, p_home);
	  break;
	case 1:
	  accept_swap(newfit, oldfit, ninterface, numpartners, Speclist, tmppartners, templist);
	  break;
	case 2:
	  accept_swap(newfit, oldfit, ninterface, numpartners, Speclist, tmppartners, templist);
	  break;
	case 3:
	  accept_ifacechg(newfit, oldfit, ntmpinterface, ninterface, numpartners, Speclist, tmppartners, templist, nwhole, wholep, wholetemp);
	  break;
	case 4:
	  accept_ifacechg(newfit, oldfit, ntmpinterface, ninterface, numpartners, Speclist, tmppartners, templist, nwhole, wholep, wholetemp);
	  break;
	case 5:
	  accept_swap(newfit, oldfit, ninterface, numpartners, Speclist, tmppartners, templist);	
	  break;
	case 6:
	  accept_swap(newfit, oldfit, ninterface, numpartners, Speclist, tmppartners, templist);	
	  break;
	}
	//cout <<"done accepting move ."<<endl;
      } else {
	/*Reject move*/
	
	//cout <<"Rejected move of type "<<move<<" change: "<<del<<" newfit: "<<newfit<<" oldfit: "<<oldfit<<endl;
	newfit=oldfit;

	moverej[move]++;
	/*Protein structure does not change for mutate connections or mutate edge*/
	for(i=0;i<nwhole;i++){
	  wholetemp[i].ninterface=wholep[i].ninterface;
	  for(j=0;j<wholep[i].ninterface;j++){
	    id=wholep[i].valiface[j];
	    wholetemp[i].valiface[j]=id;
	    p_home[id]=i;
	  }
	}
      }
      
    }//end checking whether a move was actually made

    //Done with performing the move.

    //Write out network each iteration divisible by netwrite
    if(t%netwrite==0){
      
      avgratio=calc_avg_ratio(ninterface, Speclist, numpartners);
      cout <<"Netwrite, iter: "<<t<<endl;
         
              
      cout <<"beta, mu"<<beta<<", "<<mu<<" iter: "<<t<<" ninterface "<<ninterface<<" network fitness: "<<oldfit<< " avg ratio: "<<avgratio<<" network only fitness: "<< " nsquares: " << nsquares << endl;
      
      write_ppi(nwhole, wholep);
      write_speclist(ninterface, numpartners, Speclist);
      
      stop_timer(&looptime2);
      cout <<"Time to netwrite: "<<timer_duration(looptime2)<<endl;
      start_timer(&looptime2);

      
    }
    //Write out network each time a new optimum is found
      if(oldfit<globopt){
        globopt=oldfit;
      /*Update p_home before measuring concentration fitness*/
        for(i=0;i<nwhole;i++){
          for(j=0;j<wholep[i].ninterface;j++){
	    iface=wholep[i].valiface[j];
	    p_home[iface]=i;
          }
        }
	avgratio=calc_avg_ratio(ninterface, Speclist, numpartners);

	//Recalculate stats
	
	for(i=0;i<ninterface*ninterface;i++)
	  Adj2[i]=0;

	numedge = 0.0;
	for(i=0;i<ninterface;i++){
	  Adj2[i*ninterface+i]=1; //Necessary for finding islands
	  for(j=0;j<numpartners[i];j++){
	    Adj2[i*ninterface+Speclist[i][j]]=1;
	    Adj2[Speclist[i][j]*ninterface+i]=1;
	    if(Speclist[i][j]==i)
	      numedge+=1;
	    else
	      numedge+=0.5;
	  }
	}
	cluster_cof_self(ninterface, open, closed, avg, Speclist, numpartners, self, selfavg);
	Cglobal=closed*1.0/(1.0*(open+closed));
	if(closed==0)Cglobal=0;
	Clocal=avg/ninterface;
	  
	nsquares = temphist[4][1];//Should have already been found above.
	nchains=temphist[3][1];
	n4hubs=temphist[3][0];
	n4mers=temphist[3][0]+temphist[3][1]+temphist[4][0]+temphist[4][1]+temphist[5][0]+temphist[6][0];
	for(j=0;j<maxnif;j++)
	  pmf[j]=0.0;
	for(j=0;j<ninterface;j++){
	  degk+=numpartners[j];
	  pmf[numpartners[j]-1]++;
	}
	degk/=(1.0*ninterface);
	for(j=0;j<maxnif;j++){
	  pmf[j]/=(1.0*ninterface);
	}
	if(PAEflag==1) //Calculate the best-fit PAE using a .txt file
	  degalpha=findAlpha(pmf, numedge, ninterface, nwhole,maxnif, PPIedge, maxne, sampPMFs);
	else if(PAEflag==0) //Calculate the best-fit PAE by generating random networks
	  degalpha=findAlpha2(pmf,numedge,ninterface);
	else{ //Find the index of dispersion rather than the PAE
	  degalpha=0;
	  for(j=0;j<ninterface;j++){
	    degalpha+=pow(numpartners[j]-degk,2);
	  }
	  degalpha/=ninterface;//This is the variance
	  degalpha/=degk;//This is the Index of Dispersion
	}
	  
	modcurr = find_islands(ninterface, Adj2, modlist, modsizes);
	//Find largest module size
	giant=0;
	for(i=0;i<modcurr;i++){
	  if(modsizes[i]>giant)
	    giant=modsizes[i];
	}

	cout << "NEW OPTIMAL NETWORK FOUND" << endl;
	cout <<"beta, mu, kappa "<<beta<<", "<<mu<<", "<<kappa<<" iter: "<<t<<" ninterface: "<<ninterface<<" nedge: " << numedge << " nsquares: " << nsquares<<" Alpha (degree dist): " << degalpha <<" Largest module size: " << giant << " network fitness: "<<oldfit<< " avg ratio: "<<avgratio << endl;
	write_ppi(nwhole, wholep);
	write_speclist(ninterface, numpartners, Speclist);

          	  
	//Also write to globalfile
	if(t<Nit*0.2){
	  globalfile << t << "\t\t" << ninterface << "\t\t" << numedge<<"\t\t"<<nsquares << "\t\t" << giant << "\t\t" << degalpha << "\t\t" << degk << "\t\t" << Clocal << "\t\t" << Cglobal << "\t\t" << nchains << "\t\t" << n4hubs << "\t\t" << n4mers << "\t\tNew Optimum. Fitness: " << oldfit << endl;
	}

	//Record new optimal network
	
	for(i=0;i<ninterface;i++){
	  numpartnersOpt[i]=numpartners[i];
	  for(j=0;j<numpartners[i];j++){
	    SpeclistOpt[i][j]=Speclist[i][j];
	  }
	}

	for(i=0;i<nwhole;i++){
	  wholepOpt[i].ninterface = wholep[i].ninterface;
	  for(j=0;j<wholep[i].ninterface;j++)
	    wholepOpt[i].valiface[j]=wholep[i].valiface[j];
	}

	iterOpt=t;
      }
      
      /*Record histogram data of key stats. These stats include:
       - number of squares
       - number of interfaces
       - number of edges
       - average degree <k>
       - alpha (degree distribution parameter)
       - clustering coefficient
       - largest module size (the giant component)
       
       Begin doing this after 1/5 of iterations, so that system equilibriates a little
       */
      if(t>=Nit*1/5){
	if((flagacc==1 && chg==1) || degk==0){
	    //cout<<"Recalculating values"<<endl;
              for(i=0;i<ninterface;i++){
                  self[i]=0;
                  for(j=0;j<numpartners[i];j++){
		    if(Speclist[i][j]==i)self[i]=1;
		  }
              }
              cluster_cof_self(ninterface, open, closed, avg, Speclist, numpartners, self, selfavg);
	      Cglobal=closed*1.0/(1.0*(open+closed));
              if(closed==0)Cglobal=0;
              Clocal=avg/ninterface;
	      //cout<<"Clocal: " << Clocal << " Cglobal: "<<Cglobal << endl;
              
              //Need to make Adj matrix
              for(i=0;i<maxnif*maxnif;i++)
                  Adj2[i]=0;
	      for(i=0;i<ninterface;i++){
		for(j=0;j<numpartners[i];j++){
		  Adj2[i*ninterface + Speclist[i][j]]=1;
		}
		Adj2[i*ninterface+i]=1;
	      }
              modcurr = find_islands(ninterface, Adj2, modlist, modsizes);
	      giant=0;
              for(j=0;j<modcurr;j++){
	      
	        if(modsizes[j]>giant)
	            giant=modsizes[j];
              }
	      //cout << "Largest module size: " << giant << endl;
                            
              degk=0;
              for(j=0;j<maxnif;j++)
                  pmf[j]=0.0;
              for(j=0;j<ninterface;j++){
                  degk+=numpartners[j];
                  pmf[numpartners[j]-1]++;
              }
	      for(j=0;j<maxnif;j++){
		pmf[j]/=(1.0*ninterface);
	      }
              degk/=(1.0*ninterface);
              //cout << "Average degree: " << degk << endl;
	      
	      if(PAEflag==1)
		degalpha=findAlpha(pmf, numedge, ninterface, nwhole,maxnif, PPIedge, maxne, sampPMFs);
	      else if(PAEflag==0)
		degalpha=findAlpha2(pmf,numedge,ninterface);
	      else{
		degalpha=0;
		for(j=0;j<ninterface;j++){
		  degalpha+=pow(numpartners[j]-degk,2);
		}
		degalpha/=ninterface;//This is the variance
		degalpha/=degk;//This is the Index of Dispersion
	      }
         
	      //cout << "Alpha: " << degalpha << endl;
              
	      numedge=0.0;
	      for(i=0;i<ninterface;i++){
		for(j=0;j<numpartners[i];j++){
		  if(Speclist[i][j]==i)
		    numedge+=1;
		  else
		    numedge+=0.5;
		}
	      }
	}
          
          nsquares_mean+=nsquares;
	  nchains_mean+=nchains;
	  n4hubs_mean+=n4hubs;
	  n4mers_mean+=n4mers;
          ninterface_mean+=ninterface;
          numedge_mean+=numedge;
          degk_mean+=degk;
          degalpha_mean+=degalpha;
          
          Cglobal_mean+=Cglobal;
          Clocal_mean+=Clocal;
          giant_mean+=giant;
          
          output_count+=1;

	  //Now write out to globalfile
	  globalfile << t << "\t\t" << ninterface << "\t\t" << numedge<<"\t\t"<<nsquares << "\t\t" << giant << "\t\t" << degalpha << "\t\t" << degk << "\t\t" << Clocal << "\t\t" << Cglobal << "\t\t" << nchains << "\t\t" << n4hubs << "\t\t" << n4mers;
	  if(iterOpt==t){
	    globalfile << "\tNew Optimum. Fitness: " << oldfit;
	  }
	  globalfile << endl;
      }

  }//Done with MC sampling of IIN's
  cout <<"finished with MC"<<endl;


  //Find mean stats
    nsquares_mean/=output_count;
    nchains_mean/=output_count;
    n4hubs_mean/=output_count;
    n4mers_mean/=output_count;
    ninterface_mean/=output_count;
    numedge_mean/=output_count;
    
    degk_mean/=output_count;
    degalpha_mean/=output_count;
    
    Cglobal_mean/=output_count;
    Clocal_mean/=output_count;
    giant_mean/=output_count;

    cout <<"beta, mu, kappa, omega: "<<beta<<", "<<mu<< ", " << kappa << ", " << omega<< " Acceptance: "<<acc*1.0/(1.0*neq)<<"  decreased and accepted: "<<decacc*1.0/(dec*1.0)<<" decreased: "<<dec*1.0/(1.0*neq)<<endl;
    for(i=0;i<nmove;i++)
        cout <<"move: "<<i<<" tries: "<<movetot[i]<<"  accept: "<<moveacc[i]*1.0/(movetot[i]*1.0)<<" reject: "<<moverej[i]*1.0/(movetot[i]*1.0)<<endl;
  
    cout << "Mean parms for last 4/5 of iterations: " << endl;
    cout << "Ninterfaces: " << ninterface_mean << " Nedges: " << numedge_mean << " Nsquares: " << nsquares_mean << " Cglobal: " << Cglobal_mean << " Clocal " << Clocal_mean << " Largest module size: " << giant_mean << " Average degree: " << degk_mean << " Alpha (degree dist): " << degalpha_mean << endl;

    globalfile << "Averages:" << "\t" << ninterface_mean << "\t\t" << numedge_mean<<"\t\t"<<nsquares_mean << "\t\t" << giant_mean << "\t\t" << degalpha_mean << "\t\t" << degk_mean << "\t\t" << Clocal_mean << "\t\t" << Cglobal_mean<<"\t\t"<<nchains_mean<<"\t\t"<<n4hubs_mean<<"\t\t"<<n4mers_mean<<endl;

    globalfile.close();

  
    delete [] selfflag;

}
