/*
** This is a core of Metropolis Monte Carlo sampling procedure for
** simplified polypeptides. It contains the simulation procedure
** as well as the setup of initial conformation based on provided PDB
** or simplified sequence-structure input.
**
** Copyright (c) 2004 - 2010 Alexei Podtelezhnikov
** Copyright (c) 2007 - 2013 Nikolas Burkoff, Csilla Varnai and David Wild
*/

#include<stdlib.h>		/* rand() */
#include<stdio.h>
#include<string.h>
#include<math.h>
#include<float.h>

#include"error.h"
#include"params.h"
#include"aadict.h"
#include"vector.h"
#include"rotation.h"
#include"peptide.h"
#include"vdw.h"
#include"energy.h"
#include"metropolis.h"


#define Erg(I,J)     erg[(I) * chain->NAA + (J)]
#define Ergt(I,J)   ergt[(I - start) * chain->NAA + (J)]


/***********************************************************/
/****           MOVES AND METROPOLIS CRITERIA           ****/
/***********************************************************/


/* Check if the proposed move (saved in chaint) is allowed
   by applying the Metropolis criteria on the energy change.
   If the move is allowed, update the coordinates and the
   energy matrix. */
static int allowed(Chain *chain, Chaint *chaint, Biasmap* biasmap, int start, int end, double logLstar, double *currE, simulation_params *sim_params)
{	



	int i, j;
	double q, loss, totalE = 0.0;
	
	for (i = start; i <= end; i++){

		for (j = 1; j < chain->NAA; j++) {

			if (j < start || end < j){
				q = energy2(biasmap,chaint->aat + i, (chain->aa) + j, &(sim_params->protein_model));
			} else if (j > i){
				q = energy2(biasmap,chaint->aat + i, chaint->aat + j, &(sim_params->protein_model));
			} else if (j == i){
				q = energy1(chaint->aat + i, &(sim_params->protein_model));
			} else {	/* double jeopardy, (start <= j < i) */
				chaint->Ergt(i, j) = chaint->Ergt(j, i);
				continue;
			}

			chaint->Ergt(i, j) = q;
			loss += (chain->Erg(i, j) - q);
			//totalE += q;
		}
	}
	/*Also take into account the global_energy term */

        /*special cyclic*/
	if (sim_params->protein_model.external_potential_type2 == 4) {
		loss -= (chain->Erg(1, (chain->NAA) - 1) - q);
	}

	q = global_energy(start,end,chain,chaint,biasmap,&(sim_params->protein_model));
	//loss += (chain->Erg(0, 0) - q);
	//fprintf(stderr,"MC move q = %g, loss = %g,",q,loss);
	double externalloss = (chain->Erg(0, 0) - q);
	double internalloss = loss;
	double external_k = sim_params->protein_model.external_k[0];
	if (q > 10) external_k = 0.01;
	//loss is negative!!
	/* Metropolis criteria */
	//loss += q - chain->Erg(0, 0);
	//loss = loss/sqrt(chain->NAA) + externalloss;
	//loss = loss + externalloss;

	

	
	

	if (loss < 0.0 && !sim_params->NS &&  exp(sim_params->thermobeta * (loss+externalloss)) * RAND_MAX < external_k * rand()) {
		//fprintf(stderr," rejected\n");
		return 0;	/* disregard rejected changes */
	}

    //if (sim_params->protein_model.external_potential_type == 5 && q < 10 && externalloss < 0.0 && !sim_params->NS &&   -externalloss * external_k * RAND_MAX > rand()) {
    //    return 0;
    //}
	//
	//if (sim_params->protein_model.external_potential_type == 5 && q >= 10 && !sim_params->NS && externalloss < 0.0 &&   exp(sim_params->thermobeta * externalloss) * RAND_MAX < external_k * rand()) {
	//	//fprintf(stderr," rejected\n");
	//	return 0;	/* disregard rejected changes */
	//}


	//fprintf(stderr," accepted\n");
	/*Nested Sampling criteria -- important second possibility is for FLEX and is otherwise ignored */



	if(sim_params->NS && ((-logLstar > *currE && -logLstar < *currE - loss) || (-logLstar < *currE && loss < 0  )  ))
        return 0;
	

	/* commit accepted changes */
	for (i = start; i <= end; i++)
		for (j = 1; j < chain->NAA; j++)
	    	chain->Erg(i, j) = chain->Erg(j, i) = chaint->Ergt(i, j);
	chain->Erg(0, 0) = q;
	
	*currE -= internalloss + externalloss;
	
    return 1;
}

/* Make a crankshaft move.  This is a local move that involves
   the crankshaft rotation of up to 4 peptde bonds.  Propose a
   move, and apply the Metropolis criteria. */
static int crankshaft(Chain * chain, Chaint *chaint, Biasmap *biasmap, double ampl, double logLstar, double * currE, simulation_params *sim_params)
{	
	int start, end, len, toss;
	double alpha;
	vector a;
	matrix t;
	const double discrete = 2.0 / RAND_MAX;
    
	//if(sim_params->NS){ 
	for (int i = 1; i < chain->NAA; i++){
		chaint->aat[i].etc = chain->aa[i].etc;
		chaint->aat[i].num = chain->aa[i].num;
		chaint->aat[i].id = chain->aa[i].id;
		chaint->aat[i].chainid = chain->aa[i].chainid;
	}
	//}

    
   
	/* setup sidechain dihedral angles */
	/* They change with P = 1/4 (unless fixed) */
	if ((sim_params->protein_model).use_gamma_atoms != NO_GAMMA) {
	    if (!(sim_params->protein_model).fix_chi_angles && rand()/(double)RAND_MAX < 0.25) { /* change chi angles */
		for (int i = 1; i < chain->NAA; i++){ 
		    //fprintf(stderr,"chi angles of amino acid %d",i);
		    //fprintf(stderr," %c",chain->aa[i].id);
		    //fprintf(stderr," %g",chain->aa[i].chi1); //would fail for G,A
		    //fprintf(stderr," %g\n",chain->aa[i].chi2); //would fail for all but V,I,T
		    if(chain->aa[i].id != 'G' && chain->aa[i].id != 'A' && chain->aa[i].chi1 != DBL_MAX) {
			chaint->aat[i].chi1 = sidechain_dihedral(chain->aa[i].id, sim_params->protein_model.sidechain_properties);//aa[i].chi1;
		    }
		    if((chain->aa[i].id == 'V' || chain->aa[i].id == 'I' || chain->aa[i].id == 'T') && chain->aa[i].chi2 != DBL_MAX) {
			chaint->aat[i].chi2 = sidechain_dihedral2(chain->aa[i].id,chaint->aat[i].chi1, sim_params->protein_model.sidechain_properties);//aa[i].chi2;
		    }
		}
	    } else {
		for (int i = 1; i < chain->NAA; i++){
			//TODO: we might need this here: if(chain->aa[i].id != 'G' && chain->aa[i].id != 'A') {
			    chaint->aat[i].chi1 = chain->aa[i].chi1;
			    chaint->aat[i].chi2 = chain->aa[i].chi2;
			//}
		} 
	    }
	}

	// Calculate the look-up table of allowed MC moves on the 1st call.
	// This will avoid moves involving residues on more than 1 chain
	//            TODO:                     and fixed atoms.
	if (sim_params->MC_lookup_table == NULL) {
		fprintf(stderr,"creating MC move lookup table.\n");
		// Use the sequence for this, where chain breaks are marked.
		if (sim_params->seq == NULL || sim_params->sequence == NULL) {
			fprintf(stderr,"sim_params->seq: %s\n",sim_params->seq);
			fprintf(stderr,"sim_params->sequence: %s\n",sim_params->sequence);
			stop("sequence is not present in sim_params for MC lookup table calculation\n");
		}
		//allocate memory
//		fprintf(stderr,"allocating memory: 4 * (%d-1+%d) integers.\n",sim_params->NAA,sim_params->Nchains);
		int N = (sim_params->NAA - 1 + sim_params->Nchains);
		sim_params->MC_lookup_table = (int *)malloc(sizeof(int) * 4 * N);
		sim_params->MC_lookup_table_n = (int *)malloc(sizeof(int) * 4);
		if (!sim_params->MC_lookup_table || !sim_params->MC_lookup_table_n) stop("Unable to allocate memory for sim_params->MC_lookup_table.");

		fprintf(stderr,"Sequence:    ");
		for (int i=1; i<sim_params->NAA; i++) {
			fprintf(stderr,"%c",chain->aa[i].id);
		}
		fprintf(stderr,"\n");
		fprintf(stderr,"Fixed:       ");
		for (int i=1; i<sim_params->NAA; i++) {
			if (chain->aa[i].etc & FIXED)
				fprintf(stderr,"x");
			else
				fprintf(stderr," ");
		}
		fprintf(stderr,"\n");
		fprintf(stderr,"Constrained: ");
		for (int i=1; i<sim_params->NAA; i++) {
			if (chain->aa[i].etc & CONSTRAINED)
				fprintf(stderr,"x");
			else
				fprintf(stderr," ");
		}
		fprintf(stderr,"\n");
		fprintf(stderr,"Chain:       ");
		for (int i=1; i<sim_params->NAA; i++) {
			fprintf(stderr,"%d",chain->aa[i].chainid % 10);
		}
		fprintf(stderr,"\n");

		//fprintf(stderr,"lookup table:\n");
		//fill in lookup table
		for (int i=0; i<4; i++) { // loop length can be 0-4
			int next = 0;
			fprintf(stderr,"len %d bonds, Nchains %d:", i+1, sim_params->Nchains);
			int fixed_moves = 0; //counter for moves disallowed due to fixed atoms
			for (int j=1; j<(sim_params->NAA - i); j++){ //start of loop

				//Check if any of the atoms would be fixed
				int any_fixed = 0;
				for (int k=j; k<j+i+1; k++) {
					if ((chain->aa[k].etc & FIXED) && (chain->aa[j].chainid == chain->aa[k].chainid)) any_fixed = 1;
				}
				if (any_fixed) {					
					if (chain->aa[j].chainid == chain->aa[j + i].chainid) {
						fprintf(stderr, "fixed amino acid in %d-%d, skipping", j, j + i + 1);
						
						fixed_moves++;
					}
					//also count the extra move at the beginning of the chain
					if (j == 1) {
						if (chain->aa[j].chainid == chain->aa[j+i].chainid) fixed_moves ++;
					} else {
						if (chain->aa[j].chainid != chain->aa[j-1].chainid && chain->aa[j].chainid == chain->aa[j+i].chainid) fixed_moves ++;
					}
					//also count the extra move at the end of the mid-chain
					if (chain->aa[j].chainid != chain->aa[j + 1].chainid && !chain->aa[j + 1].etc & FIXED) fixed_moves++;
					continue;
				}

				//no fixed atoms, add the move(s).

				//if it's the beginning of the chain, and the chain is long enough, also add the previous amino acid
				if (j == 1) { //first beginning
					if (chain->aa[j].chainid == chain->aa[j+i].chainid) {
						sim_params->MC_lookup_table[i*N+next] = j-1;
						fprintf(stderr,"*%d ",j-1);
						next ++;
					}
				} else { // j > 1, also check chainID differs from previous aa
					if (chain->aa[j].chainid != chain->aa[j-1].chainid && chain->aa[j].chainid == chain->aa[j+i].chainid && !chain->aa[j - i].etc & FIXED) {
						sim_params->MC_lookup_table[i*N+next] = j-1;
						fprintf(stderr,"*%d ",j-1);
						next ++;
					}
				}
				//if it's inside the chain or at the end
				if (chain->aa[j].chainid == chain->aa[j+i].chainid) {
					sim_params->MC_lookup_table[i*N+next] = j;
					fprintf(stderr,"x%d ",j);
					next ++;
				}
			}
			//fprintf(stderr,"(next=%d)",next);
			fprintf(stderr, "%d+%d != (%d-1+(1-%d)*%d\n", next, fixed_moves, sim_params->NAA, i, sim_params->Nchains);
			int N_len = (sim_params->NAA - 1) + (1 - i) * sim_params->Nchains; //number of possibilities for this len
			if ((next + fixed_moves) != N_len) {
				//fprintf(stderr,"%d+%d != (%d-1+(1-%d)*%d\n", next, fixed_moves, sim_params->NAA,i,sim_params->Nchains);
				stop("Something has gone wrong.  Maybe too short chains?\n");
			//} else {
			//	fprintf(stderr,"%d == (%d-1+(1-%d)*%d\n", (next), sim_params->NAA,i,sim_params->Nchains);
			}
			sim_params->MC_lookup_table_n[i] = next; //the number of valid moves
			for (int j = next; j<(sim_params->NAA - 1 + sim_params->Nchains); j++) {
				sim_params->MC_lookup_table[i*N+next] = -1;
				fprintf(stderr,"%d ",-1);
				next ++;
			}
			fprintf(stderr,"\n");
		}
	}

	//stop("Everything is OK.");

//TODO multi-chain protein
	int pivot_around_end = 0;
	int pivot_around_start = 0;

	toss = rand();
	/* segment length */
	len = toss & 0x3;	/* segment length minus one */
	if (len > chain->NAA - 2)
		len = chain->NAA - 2;

	//fprintf(stderr,"MC move len = %d,",len);
	/* amino acids are numbered from 1 to NAA-1 */
	/* segment could start 1 before the first amino acid and end 1 after the last amino acid (pivot moves) or within the chain (crankshaft moves) */

	int N = (sim_params->NAA - 1 + sim_params->Nchains);
	//int N_len = (chain->NAA - 1) + (1 - len) * sim_params->Nchains; //number of possibilities for this len (this has been checked when building the lookup table)
	int N_len = sim_params->MC_lookup_table_n[len]; //the number of valid moves (taking into account fixed amino acids)
	//fprintf(stderr," random number in [ 0, %d ],",N_len-1);

	/* segment start */
	start = sim_params->MC_lookup_table[len*N + (toss >> 2) % N_len ];
	/* segment end */
	if ((sim_params->protein_model).fix_CA_atoms) {
	    end = start + 1;
	} else {
	    end = start + len + 1;
	}
	if (start < 0 || end < 0) { //hit a -1 in the table!
		stop("Something has gone wrong when selecting aminio acids for the MC move.\n");
	}
	//fprintf(stderr,"move residues %d -- %d (len: %d bonds),",start,end,len+1);
	for (int ai=start; ai<end; ai++) {
		if (chain->aa[ai].etc & FIXED) {
			fprintf(stderr,"residues %d -- %d (len: %d bonds),",start,end,len+1);
			fprintf(stderr,"\n%d is fixed\n",ai);
			stop("crankshaft: tried to move fixed amino acid.\n");
		}
	}
	/* pivot or crankshaft */
	if (start == 0) {
		pivot_around_end = 1;
		//fprintf(stderr," pivot around end\n");
	} else if (end == sim_params->NAA) {
		pivot_around_start = 1;
		//fprintf(stderr," pivot around start\n");
	} else if (chain->aa[start].chainid != chain->aa[end].chainid) {
		//fprintf(stderr,"  chainid[start] = %d, chainid[end] = %d\n",chain->aa[start].chainid,chain->aa[end].chainid);
		if (len == 0) {
			/* special case for multi-chain protein at chain break for len=0 (2 amino acids) */
			if (rand() & 0x2) {
				pivot_around_start = 1;
				//fprintf(stderr," pivot around start\n");
			} else {
				pivot_around_end = 1;
				//fprintf(stderr," pivot around end\n");
			}
		} else {
			if (chain->aa[start].chainid == chain->aa[start+1].chainid) {
				pivot_around_start = 1;
				//fprintf(stderr," pivot around start\n");
			} else if (chain->aa[end].chainid == chain->aa[end-1].chainid) {
				pivot_around_end = 1;
				//fprintf(stderr," pivot around end\n");
			} else {
				stop("something has gone wrong at the MC move selection\n");
			}
		}
	//} else {// else crankshaft
		//fprintf(stderr,"  chainid[start] = %d, chainid[end] = %d\n",chain->aa[start].chainid,chain->aa[end].chainid);
		//fprintf(stderr," crankshaft\n");
	}

	
	/* setup fixed ends for crankshaft or pivot */
	if (pivot_around_end != 1) { // there is a fixed start site
		casttriplet(chaint->xaat[start], chain->xaa[start]); //TODO: use start - 1 ??
		castvec(chaint->aat[start].ca, chain->aa[start].ca);
		//TODO we will also need xaa[start-1]
		if (start == 1) {//if chain start, use x_prev
			casttriplet(chaint->xaat_prev[chain->aa[end].chainid], chain->xaa_prev[chain->aa[end].chainid]);
			//fprintf(stderr,"copying xaat_prev0 %d \n",chain->aa[end].chainid);
			//fprintf(stderr," %g %g %g %g %g %g %g %g %g\n",chaint->xaat_prev[chain->aa[end].chainid][0][0],chaint->xaat_prev[chain->aa[end].chainid][0][1],chaint->xaat_prev[chain->aa[end].chainid][0][2],chaint->xaat_prev[chain->aa[end].chainid][1][0],chaint->xaat_prev[chain->aa[end].chainid][1][1],chaint->xaat_prev[chain->aa[end].chainid][1][2],chaint->xaat_prev[chain->aa[end].chainid][2][0],chaint->xaat_prev[chain->aa[end].chainid][2][1],chaint->xaat_prev[chain->aa[end].chainid][2][2]);
		} else if (chain->aa[start].chainid != chain->aa[start - 1].chainid) {
			casttriplet(chaint->xaat_prev[chain->aa[end].chainid], chain->xaa_prev[chain->aa[end].chainid]);
			//fprintf(stderr,"copying xaat_prev1 %d\n",chain->aa[end].chainid);
			//fprintf(stderr," %g %g %g %g %g %g %g %g %g\n",chaint->xaat_prev[chain->aa[end].chainid][0][0],chaint->xaat_prev[chain->aa[end].chainid][0][1],chaint->xaat_prev[chain->aa[end].chainid][0][2],chaint->xaat_prev[chain->aa[end].chainid][1][0],chaint->xaat_prev[chain->aa[end].chainid][1][1],chaint->xaat_prev[chain->aa[end].chainid][1][2],chaint->xaat_prev[chain->aa[end].chainid][2][0],chaint->xaat_prev[chain->aa[end].chainid][2][1],chaint->xaat_prev[chain->aa[end].chainid][2][2]);
		} else {
			casttriplet(chaint->xaat[start-1], chain->xaa[start-1]);
		}
	} else {
		//we will also need the xaa[start-1], stored in xaa_prev for chain beginnings
		casttriplet(chaint->xaat_prev[chain->aa[end].chainid], chain->xaa_prev[chain->aa[end].chainid]);
		//fprintf(stderr,"copying xaat_prev2\n");
		//fprintf(stderr," %g %g %g %g %g %g %g %g %g\n",chaint->xaat_prev[chain->aa[end].chainid][0][0],chaint->xaat_prev[chain->aa[end].chainid][0][1],chaint->xaat_prev[chain->aa[end].chainid][0][2],chaint->xaat_prev[chain->aa[end].chainid][1][0],chaint->xaat_prev[chain->aa[end].chainid][1][1],chaint->xaat_prev[chain->aa[end].chainid][1][2],chaint->xaat_prev[chain->aa[end].chainid][2][0],chaint->xaat_prev[chain->aa[end].chainid][2][1],chaint->xaat_prev[chain->aa[end].chainid][2][2]);
	}
	if (pivot_around_start != 1) { // there is a fixed end site
		casttriplet(chaint->xaat[end], chain->xaa[end]);
		castvec(chaint->aat[end].ca, chain->aa[end].ca);
	}



	/* magnitude of rotation */
	/* rotate triplets, alpha in [-ampl; +ampl] */
	alpha = ampl * (discrete * rand() - 1.0);

	/* axis of rotation */
	if (pivot_around_start != 1 && pivot_around_end != 1) {
		/* CA_start->CA_end vector for internal crankshaft */
		subtract(a, chain->aa[end].ca, chain->aa[start].ca);
		normalize(a);
	} else
		/* random vector for pivot at chain end */
		randvector(a);

	/* rotation matrix */
	rotmatrix(t, a, alpha);


	/* rotating the CA_i->CA_i+1 vectors */
	for (int i = start; i < end; i++){
		if (pivot_around_end == 1 && i == start) {
			//do not change the xaa of the previous chain, use this chain's xaa_prev instead
			rotation(chaint->xaat_prev[chain->aa[end].chainid], t, chain->xaa_prev[chain->aa[end].chainid]);
		} else {
			rotation(chaint->xaat[i], t, chain->xaa[i]);
		}
	}
	/* build trial amino acid CAs using the CA-CA vectors */
	if (pivot_around_end != 1) { // start rotation from the start site
		for (int i = start; i < end - 1; i++){ //moving residues start+1 to end-1
			carbonate_f(chaint->aat + i + 1, chaint->aat + i, chaint->xaat[i]);
		}
		if (pivot_around_start == 1) end --;
	}
	else { //pivot around end
		for (int i = end - 1; i > start; i--){ //moving residues end-1 to start+1
			carbonate_b(chaint->aat + i, chaint->aat + i + 1, chaint->xaat[i]);
		}
		start ++;
	}
	
	//building the peptide bonds of the amino acids
	//by now start and end have been adjusted if pivoting
	for (int i = start; i <= end; i++){
		//if starting at the beginning of the chain with pivot or crankshaft
		//fprintf(stderr,"metropolis pivot %d", pivot_around_end);
		//fprintf(stderr," start %d", start);
		//fprintf(stderr," current %d", i);
		//if (pivot_around_end == 1 && i == start){
		//	fprintf(stderr,"\n");
		//} else {
		//	fprintf(stderr," chain(current) %d", chain->aa[i].chainid);
		//	fprintf(stderr," chain(prev) %d\n", chain->aa[i-1].chainid);
		//}
		if ((pivot_around_end == 1 && i == start) || (chain->aa[i].chainid != chain->aa[i-1].chainid))  {
			//use this chain's xaa_prev for the the direction of the N-terminal NH
			//fprintf(stderr,"acidate %d with xaat_prev1 %g %g %g %g %g %g %g %g %g\n",i,chaint->xaat_prev[chain->aa[i].chainid][0][0],chaint->xaat_prev[chain->aa[i].chainid][0][1],chaint->xaat_prev[chain->aa[i].chainid][0][2],chaint->xaat_prev[chain->aa[i].chainid][1][0],chaint->xaat_prev[chain->aa[i].chainid][1][1],chaint->xaat_prev[chain->aa[i].chainid][1][2],chaint->xaat_prev[chain->aa[i].chainid][2][0],chaint->xaat_prev[chain->aa[i].chainid][2][1],chaint->xaat_prev[chain->aa[i].chainid][2][2]);
			//fprintf(stderr,"acidate %d with xaat2 %g %g %g %g %g %g %g %g %g\n",i,chaint->xaat[i][0][0],chaint->xaat[i][0][1],chaint->xaat[i][0][2],chaint->xaat[i][1][0],chaint->xaat[i][1][1],chaint->xaat[i][1][2],chaint->xaat[i][2][0],chaint->xaat[i][2][1],chaint->xaat[i][2][2]);
			acidate(chaint->aat + i, chaint->xaat_prev[chain->aa[i].chainid], chaint->xaat[i], sim_params);
		} else {
			//fprintf(stderr,"acidate %d with xaat1 %g %g %g %g %g %g %g %g %g\n",i,chaint->xaat[i-1][0][0],chaint->xaat[i-1][0][1],chaint->xaat[i-1][0][2],chaint->xaat[i-1][1][0],chaint->xaat[i-1][1][1],chaint->xaat[i-1][1][2],chaint->xaat[i-1][2][0],chaint->xaat[i-1][2][1],chaint->xaat[i-1][2][2]);
			//fprintf(stderr,"acidate %d with xaat2 %g %g %g %g %g %g %g %g %g\n",i,chaint->xaat[i][0][0],chaint->xaat[i][0][1],chaint->xaat[i][0][2],chaint->xaat[i][1][0],chaint->xaat[i][1][1],chaint->xaat[i][1][2],chaint->xaat[i][2][0],chaint->xaat[i][2][1],chaint->xaat[i][2][2]);
			acidate(chaint->aat + i, chaint->xaat[i - 1], chaint->xaat[i], sim_params);
		}
	}




        /* testing if move is allowed */
	if (!allowed(chain,chaint,biasmap,start, end, logLstar,currE, sim_params))
		return 0;	/* disregard rejected changes */
      
    	
	/* commit accepted changes */
	
	//fprintf(stderr,"committing amino acid xaa %d - %d\n",start-1,end);
	if ((pivot_around_end == 1) || (chain->aa[start].chainid != chain->aa[start-1].chainid))  { //update this chain's xaa_prev
		casttriplet(chain->xaa_prev[chain->aa[end].chainid], chaint->xaat_prev[chain->aa[end].chainid]);
		//fprintf(stderr,"saving chain-%d xaat_prev1 %g %g %g %g %g %g %g %g %g\n",chain->aa[end].chainid,chaint->xaat_prev[chain->aa[i].chainid][0][0],chaint->xaat_prev[chain->aa[i].chainid][0][1],chaint->xaat_prev[chain->aa[i].chainid][0][2],chaint->xaat_prev[chain->aa[i].chainid][1][0],chaint->xaat_prev[chain->aa[i].chainid][1][1],chaint->xaat_prev[chain->aa[i].chainid][1][2],chaint->xaat_prev[chain->aa[i].chainid][2][0],chaint->xaat_prev[chain->aa[i].chainid][2][1],chaint->xaat_prev[chain->aa[i].chainid][2][2]);
	} else {
		casttriplet(chain->xaa[start-1], chaint->xaat[start-1]);
	}
	for (int i = start; i <= end; i++){
		casttriplet(chain->xaa[i], chaint->xaat[i]);
	}





	//fprintf(stderr,"committing amino acid aa %d - %d\n",start,end);
	for (int i = start; i <= end; i++) {
		chain->aa[i] = chaint->aat[i];
	}

	/*translational move*/
	//
	if ((pivot_around_start == 1 || pivot_around_end == 1) && sim_params->protein_model.external_potential_type == 5) {
		double movement = 0.0;
		int moved = 0;
		for (int i = 0; i < 3; i++) {
			movement = (double)rand() / RAND_MAX;
			//fprintf(stderr, "translational move %g", movement);
			if (movement < 0.1) {
				movement = 4 * (movement - 0.05);
				moved = 1;
				for (int j = 1; j < chain->NAA; j++) {
					if (chaint->aat[j].id != 'P') {
						chaint->aat[j].h[i] = chain->aa[j].h[i] + movement;
					}
					chaint->aat[j].n[i] = chain->aa[j].n[i] + movement;
					chaint->aat[j].ca[i] = chain->aa[j].ca[i] + movement;
					chaint->aat[j].c[i] = chain->aa[j].c[i] + movement;
					chaint->aat[j].o[i] = chain->aa[j].o[i] + movement;
					if (chaint->aat[j].id != 'G') {
						chaint->aat[j].cb[i] = chain->aa[j].cb[i] + movement;
					}

					//if (chaint->aat[j].id != 'P') {
					//	chaint->aat[j].h[i] += movement;
					//}
					//chaint->aat[j].n[i] += movement;
					//chaint->aat[j].ca[i] += movement;
					//chaint->aat[j].c[i] += movement;
					//chaint->aat[j].o[i] += movement;
					//if (chaint->aat[j].id != 'G') {
					//	chaint->aat[j].cb[i] += movement;
					//}
				}
			}

		}
		if (!moved) {
			return 1;
		}

		double transExtEne = global_energy(1, chain->NAA - 1, chain, chaint, biasmap, &(sim_params->protein_model));

		//if (moved && allowed(chain, chaint, biasmap, 1, chain->NAA -1, logLstar, currE, sim_params)) {
		if (transExtEne < chain->Erg(0, 0) || exp(sim_params->thermobeta * (transExtEne - chain->Erg(0, 0))) * RAND_MAX < rand()) {
			//fprintf(stderr,"committing amino acid xaa %d - %d\n",start-1,end);
			if ((pivot_around_end == 1) || (chain->aa[start].chainid != chain->aa[start - 1].chainid)) { //update this chain's xaa_prev
				casttriplet(chain->xaa_prev[chain->aa[end].chainid], chaint->xaat_prev[chain->aa[end].chainid]);
				//fprintf(stderr,"saving chain-%d xaat_prev1 %g %g %g %g %g %g %g %g %g\n",chain->aa[end].chainid,chaint->xaat_prev[chain->aa[i].chainid][0][0],chaint->xaat_prev[chain->aa[i].chainid][0][1],chaint->xaat_prev[chain->aa[i].chainid][0][2],chaint->xaat_prev[chain->aa[i].chainid][1][0],chaint->xaat_prev[chain->aa[i].chainid][1][1],chaint->xaat_prev[chain->aa[i].chainid][1][2],chaint->xaat_prev[chain->aa[i].chainid][2][0],chaint->xaat_prev[chain->aa[i].chainid][2][1],chaint->xaat_prev[chain->aa[i].chainid][2][2]);
			}
			else {
				casttriplet(chain->xaa[start - 1], chaint->xaat[start - 1]);
			}
			for (int i = start; i <= end; i++) {
				casttriplet(chain->xaa[i], chaint->xaat[i]);
			}


			chain->Erg(0, 0) = transExtEne;

			//fprintf(stderr, "committing moved !!\n");
			//fprintf(stderr,"committing amino acid aa %d - %d\n",start,end);
			for (int i = start; i <= end; i++) {
				chain->aa[i] = chaint->aat[i];
			}
		}
	}





	return 1;
}

/* MC move wrapper.  Call crankshaft to make an MC move, and calculate the acceptance rate.
   Possibly adjust "negative" amplitudes towards the desired acceptance rate. */
void move(Chain *chain,Chaint *chaint, Biasmap *biasmap, double logLstar, double *currE, int changeamp, simulation_params *sim_params)
{  /*Changed so amplitude does not depend on history of chain 

	changeamp = 0 for normal use
	changeamp = 1 if we are wanting to use the MC move in calculation of new
				amplitude
	changeamp = -1 if we are reseting accept and reject to start the calculation of 
					a new amplitude 

*/
	//static int score = 0
	//static int accept = 0, reject = 0;    
	
	if(changeamp == -1){ sim_params->accept_counter = 0; sim_params->reject_counter = 0;}
	
	if (crankshaft(chain,chaint,biasmap,sim_params->amplitude,logLstar,currE, sim_params)) {	/* accepted */
		sim_params->accept_counter++; 
		/*if (changeamp && amplitude < 0.0 && ++score > 16 && amplitude > -M_PI) {
			score = 0;
			amplitude *= 1.1;
		}*/
	} else {		/* rejected */
		sim_params->reject_counter++; 
		/*if (changeamp && amplitude < 0.0 && --score < -32) {
			score = 0;
			amplitude *= 0.9;
		}*/
	}

	if (sim_params->accept_counter + sim_params->reject_counter == 1024) {
		sim_params->acceptance = sim_params->accept_counter / 1024.;
		if(changeamp){
		  if (sim_params->acceptance_rate_tolerance <= 0) stop("The acceptance rate tolerance must be positive.");
		  if (sim_params->acceptance_rate_tolerance >= 1) stop("The acceptance rate tolerance must be smaller than 1.");
		  if (sim_params->amplitude_changing_factor <= 0) stop("The amplitude changing factor must be positive.");
		  if (sim_params->amplitude_changing_factor >= 1) stop("The amplitude changing factor must be smaller than 1.");
		  if(sim_params->amplitude < 0.0 && sim_params->acceptance < sim_params->acceptance_rate - sim_params->acceptance_rate_tolerance) sim_params->amplitude *= sim_params->amplitude_changing_factor;
		  else if(sim_params->acceptance > sim_params->acceptance_rate + sim_params->acceptance_rate_tolerance) sim_params->amplitude /= sim_params->amplitude_changing_factor;	
		  if(sim_params->amplitude < -M_PI) sim_params->amplitude = -M_PI;
		}
		sim_params->accept_counter = 0;
		sim_params->reject_counter = 0;
	}
	
}

void finalize(Chain *chain, Chaint *chaint, Biasmap *biasmap){

	freemem_chaint(chaint);
	free(chaint);
	freemem_chain(chain); //free amino acid chain and energy matrix
	free(chain);
	biasmap_finalise(biasmap); //free contact map

}


