/*
 *  Copyright (C) 2010 by Tim Massingham, European Bioinformatics Institute
 *  tim.massingham@ebi.ac.uk
 *
 *  This file is part of the simNGS software for simulating likelihoods
 *  for next-generation sequencing machines.
 *
 *  simNGS is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  simNGS is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with simNGS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include "sequence.h"
#include "random.h"
#include "utility.h"
#include "intensities.h"
#include "weibull.h"
#include "normal.h"


void fprint_usage( FILE * fp){
    validate(NULL!=fp,);
    fputs(
"\t\"simNGS\"\n"
"Simulate likelihoods for Illumina data from fasta format files\n"
"\n"
"Usage:\n"
"\tsimNGS [-b shape:scale] [-c correlation] [-d]\n"
"\t       [-f nimpure:ncycle:threshold] [-i filename]\n"
"\t       [-l lane] [-n ncycle] [-p] [-r mu] [-s seed]\n"
"\t       [-t tile] [-v factor ]  runfile\n"
"\tsimNGS --help\n"
"\tsimNGS --licence\n"
"simNGS reads from stdin and writes to stdout. Messages and progess\n"
"indicators are written to stderr.\n"
"\n"
"Example:\n"
"\tcat sequences.fa | simNGS runfile > sequences.like\n"
,fp);
}

void fprint_licence (FILE * fp){
    validate(NULL!=fp,);
    fputs(
#include "copyright.txt"
    ,fp);
}

void fprint_help( FILE * fp){
    validate(NULL!=fp,);
    fputs(
/*
12345678901234567890123456789012345678901234567890123456789012345678901234567890
*/
"\n"
"-b, --brightness shape:scale [default: as runfile]\n"
"\tShape and scale of cluster brightnes distribution.\n"
"Currently a Weibull distribution is used.\n"
"\n"
"-c, --correlation [default: 1.0]\n"
"\tCorrelation between the cluster brightness of one end of a paired-end\n"
"run and the other. Default is complete correlation, the ends having equal\n"
"brightness. Correlation should belong to [-1,1].\n"
"\n"
"-d, --describe\n"
"\tPrint a description of the runfile and exit.\n"
"\n"
"-f, --filter nimpure:ncycle:threshold [default: no filtering]\n"
"\tUse purity filtering on generated intensities, allowing a maximum of\n"
"nimpure cyles in the first ncycles with a purity greater than threshold.\n"
"\n"
"-i, --intensities filename [default: none]\n"
"\tWrite the processed intensities generated to \"filename\".\n"
"\n"
"-l, --lane lane [default: as runfile]\n"
"\tSet lane number\n"
"\n"
"-n, --ncycles ncycles [default: as runfile]\n"
"\tNumber of cycles to do, up to maximum allowed for runfile.\n"
"\n"
"-p, --paired\n"
"\tTreat run as paired-end. For single-ended runs treated as\n"
"paired, the covariance matrix is duplicated to make two uncorrelated pairs.\n"
"For paired-end runs treated as single, the second end is ignored.\n"
"\n"
"-r, --robust mu [default: 0]\n"
"\tCalculate robustified likelihood, equivalent to adding mu to every\n"
"likelihood.\n"
"\n"
"-s, --seed seed [default: clock]\n"
"\tSet seed from random number generator.\n"
"\n"
"-t, --tile tile [default: as runfile\n"
"\tSet tile number.\n"
"\n"
"-v, --variance factor [default: 1.0]\n"
"\tFactor with which to scale variance matrix by.\n"
, fp);
}

static struct option longopts[] = {
    { "brightness", required_argument, NULL, 'b' },
    { "correlation", required_argument, NULL, 'c' },
    { "describe",   no_argument,       NULL, 'd' },
    { "filter",     required_argument, NULL, 'f' },
    { "intensities", required_argument, NULL, 'i'},
    { "lane",       required_argument, NULL, 'l' },
    { "ncycle",     required_argument, NULL, 'n' },
    { "paired",     no_argument,       NULL, 'p' },
    { "robust",     required_argument, NULL, 'r' },
    { "seed",       required_argument, NULL, 's' },
    { "tile",       required_argument, NULL, 't' },
    { "variance",   required_argument, NULL, 'v' },
    { "help",       no_argument,       NULL, 'h' },
    { "licence",    no_argument,       NULL, 0 },
};

bool parse_bool( const CSTRING str){
    validate(NULL!=str,false);
    if ( toupper(str[0]) == 'T' ){ return true; }
    if ( toupper(str[0]) == 'F' ){ return false; }
    if ( strcmp(str,"on") == 0 ){ return true; }
    if ( strcmp(str,"off") == 0 ){ return false;}
    int val=0;
    int ret = sscanf(str,"%d",&val);
    if(ret==1 && val==1){ return true;}
    // Combining remaining and failure cases below
    return false;
}

const CSTRING boolstr[] = { "false", "true" };

real_t parse_real( const CSTRING str){
    validate(NULL!=str,NAN);
    real_t x = NAN;
    sscanf(str,real_format_str,&x);
    return x;
}

unsigned int parse_uint( const CSTRING str){
    validate(NULL!=str,0);
    unsigned int n=0;
    sscanf(str,"%u",&n);
    return n;
}

typedef struct {
    unsigned int ncycle;
    real_t shape,scale,corr;
    bool paired,desc;
    real_t mu,sdfact;
    uint32_t seed;
    uint32_t tile,lane;
    real_t purity_threshold;
    uint32_t purity_cycles,purity_max;
    CSTRING intensity_fn;
} * SIMOPT;

SIMOPT new_SIMOPT(void){
    SIMOPT opt = calloc(1,sizeof(*opt));
    validate(NULL!=opt,NULL);
    // Set defaults
    opt->ncycle = 0;
    opt->shape = 0.0;
    opt->scale = 0.0;
    opt->corr = 1.0;
    opt->paired = false;
    opt->desc = false;
    opt->mu = 0.0;
    opt->seed = 0;
    opt->sdfact = 1.0;
    opt->tile = 0;
    opt->lane = 0;
    opt->purity_threshold = 0.;
    opt->purity_cycles = 0;
    opt->purity_max = 0;
    opt->intensity_fn = NULL;
    
    return opt;
}

void free_SIMOPT(SIMOPT opt){
    validate(NULL!=opt,);
    safe_free(opt);
}

SIMOPT copy_SIMOPT ( const SIMOPT simopt){
    validate(NULL!=simopt,NULL);
    SIMOPT newopt = calloc(1,sizeof(*newopt));
    validate(NULL!=newopt,NULL);
    memcpy(newopt,simopt,sizeof(*simopt));
    if(NULL!=simopt->intensity_fn){
        newopt->intensity_fn = copy_CSTRING(simopt->intensity_fn);
    }
    return newopt;
}

void show_SIMOPT (FILE * fp, const SIMOPT simopt){
    validate(NULL!=fp,);
    validate(NULL!=simopt,);
    fputs("\tOptions:\n",fp);
    fprintf( fp,"ncycle\t%u\n",simopt->ncycle);
    fprintf( fp,"paired\t%s\n",boolstr[simopt->paired]);
    fprintf( fp,"Brightness correlation\t%f\n",simopt->corr);
    fprintf( fp,"mu\t%f\n",simopt->mu);
    fprintf( fp,"shape\t%f\n",simopt->shape);
    fprintf( fp,"scale\t%f\n",simopt->scale);
    fprintf( fp,"variance factor\t%f\n",simopt->sdfact*simopt->sdfact);
    fprintf( fp,"tile\t%u\tlane%u\n",simopt->tile,simopt->lane);
    fprintf( fp,"seed\t%u\n",simopt->seed);
    if(simopt->purity_cycles!=0){
       fprintf( fp,"Purity filtering: threshold %f. Maximum of %u inpure in %u cycles\n",simopt->purity_threshold, simopt->purity_max,simopt->purity_cycles);
    } else {
       fputs("No purity filtering.\n",stdout);
    }
    if(NULL!=simopt->intensity_fn){
        fprintf(fp,"Will write intensities to \"%s\"\n",simopt->intensity_fn);
    }
}

SIMOPT parse_arguments( const int argc, char * const argv[] ){
    int ch;
    SIMOPT simopt = new_SIMOPT();
    validate(NULL!=simopt,NULL);
    
    while ((ch = getopt_long(argc, argv, "b:c:df:i:l:n:pr:s:t:uv:h", longopts, NULL)) != -1){
        int ret;
        unsigned long int i=0,j=0;
        switch(ch){
        case 'b':   ret = sscanf(optarg,real_format_str ":" real_format_str ,&simopt->shape,&simopt->scale);
                    if(ret!=2){ errx(EXIT_FAILURE,"Insufficient arguments for brightness.");}
                    if(simopt->shape<=0.){ errx(EXIT_FAILURE,"Brightness shape must be greater than zero."); }
                    if(simopt->scale<=0.){ errx(EXIT_FAILURE,"Brightness scale must be greater than zero."); }
                    break;
        case 'c':   sscanf(optarg,"%f",&simopt->corr);
                    if(simopt->corr<-1.0 || simopt->corr>1.0){errx(EXIT_FAILURE,"Correlation between end brightness should be in [-1,1]. Was given %f.",simopt->corr);}
                    break;
        case 'd':   simopt->desc = true;
                    break;
        case 'f':   ret = sscanf(optarg, "%lu:%lu:" real_format_str,&i,&j,&simopt->purity_threshold);
                    if(ret!=3){ errx(EXIT_FAILURE,"Insufficient arguments for filtering.");}
                    simopt->purity_max = i;
                    simopt->purity_cycles = j;
                    if( simopt->purity_threshold<0. || simopt->purity_threshold>1.0){
                        errx(EXIT_FAILURE,"Purity threshold is %f but should be between 0 and 1.",simopt->purity_threshold);
                    }
                    break;
        case 'i':   simopt->intensity_fn = copy_CSTRING(optarg);
                    break;
        case 'l':   simopt->lane = parse_uint(optarg);
                    if(simopt->lane==0){errx(EXIT_FAILURE,"Lane number must be greater than zero.");}
                    break;
        case 'n':   sscanf(optarg,"%u",&simopt->ncycle);
                    if(simopt->ncycle==0){errx(EXIT_FAILURE,"Number of cycles to simulate must be greater than zero.");}
                    break;

        case 'p':   simopt->paired = true;
                    break;
        case 'r':   simopt->mu = parse_real(optarg);
                    if(simopt->mu<0.0){errx(EXIT_FAILURE,"Robustness \"mu\" must be non-negative.");}
                    break;
        case 's':   simopt->seed = parse_uint(optarg);
                    break;
        case 't':   simopt->tile = parse_uint(optarg);
                    if(simopt->tile==0){errx(EXIT_FAILURE,"Tile number must be greater than zero.");}
                    break;
        case 'v':   simopt->sdfact = parse_real(optarg);
                    if(simopt->sdfact<0.0){errx(EXIT_FAILURE,"Variance scaling factor must be non-negative.");}
                    simopt->sdfact = sqrt(simopt->sdfact);
                    break;
        case 'h':
            fprint_usage(stderr);
            fprint_help(stderr);
            exit(EXIT_SUCCESS);
        case 0:
            fprint_licence(stderr);
            exit(EXIT_SUCCESS);
        default:
            fprint_usage(stderr);
            exit(EXIT_FAILURE);
        }
    }

    return simopt;
}

static inline real_t phred(const real_t p) __attribute__((const));
static inline real_t prop_upper( const real_t p, const uint32_t n) __attribute__((const));
static inline real_t prop_lower( const real_t p, const uint32_t n) __attribute__((const));

static inline real_t phred(const real_t p){
    return -10.0 * log10(p);
}

// Proportion confidence interval using Wilson's method
static inline real_t prop_upper( const real_t p, const uint32_t n){
    const real_t z = 1.959964;
    real_t desc = p*(1-p)/n + (z*z/(4.0*n*n));
    desc = p + z*z/(2.*n) + z * sqrt(desc);
    return desc / (1.0 + z*z/n);
}

static inline real_t prop_lower( const real_t p, const uint32_t n){
    const real_t z = 1.959964;
    real_t desc = p*(1-p)/n + (z*z/(4.0*n*n));
    desc = p + z*z/(2.*n) - z * sqrt(desc);
    return desc / (1.0 + z*z/n);
}


int main( int argc, char * argv[] ){
    SIMOPT simopt = parse_arguments(argc,argv);

    argc -= optind;
    argv += optind;
    if(0==argc){
        fputs("Expecting runfile on commandline but none found.\n",stderr);
        fprint_usage(stderr);
        return EXIT_FAILURE;
    }

    // Load up model
    MODEL model = new_MODEL_from_file(argv[0]);
    if( simopt->desc ){
        show_MODEL(stderr,model);
        return EXIT_SUCCESS;
    } 
    fprintf(stderr,"Description of runfile:\n%s",model->label);
    
    // Resolve options and model
    // Should factor out into separate routine
    if(simopt->shape!=0){ model->shape = simopt->shape;}
    simopt->shape = model->shape;
    if(simopt->scale!=0){ model->scale = simopt->scale;}
    simopt->scale = model->scale;
    
    if(simopt->paired!=model->paired){
        if(simopt->paired==false){
            fputs("Treating paired-end model as single-ended.\n",stderr);
            model->paired = false;
            free_MAT(model->cov2);
            model->cov2 = NULL;
        } else {
            fputs("Treating single-ended model as paired-end.\n",stderr);
            model->paired = true;
            model->cov2 = copy_MAT(model->cov1);
            model->chol2 = copy_MAT(model->chol1);
            model->invchol2 = calloc(model->ncycle,sizeof(*model->invchol2));
            for ( uint32_t i=0 ; i<model->ncycle ; i++){
                model->invchol2[i] = copy_MAT(model->invchol1[i]);
            }
        }
    }
    simopt->paired = model->paired;
    
    if(simopt->ncycle!=0){
        if(simopt->ncycle>model->ncycle){
            fprintf(stderr,"Asked for more cycles than runfile allows. Doing %u.\n",model->ncycle);
        } else {
            MODEL newmodel = trim_MODEL(simopt->ncycle,model);
            free_MODEL(model);
            model = newmodel; 
        }
    }
    simopt->ncycle = model->ncycle;
    if(0!=simopt->lane){ model->lane = simopt->lane;}
    if(0!=simopt->tile){ model->tile = simopt->tile;}
    simopt->tile = model->tile;
    simopt->lane = model->lane;

    
    // Initialise random number generator
    if ( simopt->seed==0 ){
        uint32_t seed = (uint32_t) time(NULL);
        fprintf(stderr,"Using seed %u\n",seed);
        simopt->seed = seed;
    }
    init_gen_rand( simopt->seed );
    //show_SIMOPT(stderr,simopt);
    //show_MODEL(stderr,model);

    // Scan through fasta file
    MAT intensities = NULL, loglike = NULL;
    NUC * calls = NULL;
    SEQ seq = NULL;
    FILE * fp = stdin;// fopen("test/test100_small.fa","r")
    uint32_t seq_count=0, unfiltered_count=0;
    // Memory for error counting
    uint32_t * error = calloc(model->ncycle,sizeof(uint32_t));
    uint32_t * error2 = calloc(model->ncycle,sizeof(uint32_t));
    
    FILE * fpout = (NULL!=simopt->intensity_fn) ? fopen(simopt->intensity_fn,"w") : NULL;
    if ( NULL==fpout && NULL!=simopt->intensity_fn){
        fprintf(stderr,"Failed to open \"%s\" for writing.\n",simopt->intensity_fn);
    }

    while ((seq=sequence_from_fasta(fp))!=NULL){
        //show_SEQ(stderr,seq);
        if ( seq->length < model->ncycle ){
            fprintf(stderr,"Sequence %s shorter than number of cycles, skipping\n",seq->name);
            free_SEQ(seq);
            continue;
        }
        // Pick lambda using Gaussian Copula
        real_t lambda1=NAN,lambda2=NAN;
        {
            const real_t corr = simopt->corr;
            // Two correlated Gaussians
            real_t x = rstdnorm();
            real_t y = corr*x + sqrt(1-corr*corr) * rstdnorm();
            // Convert to uniform deviates (the copula)
            real_t px = pstdnorm(x,false,false);
            real_t py = pstdnorm(y,false,false);
            // Convert to Weibull via inversion formula
            lambda1 = qweibull(px,simopt->shape,simopt->scale,false,false);
            lambda2 = qweibull(py,simopt->shape,simopt->scale,false,false);
fprintf(stderr,"%f %f   %f %f   %f %f\n",x,y,px,py,lambda1,lambda2);
        }
            
        intensities = generate_pure_intensities(simopt->sdfact,lambda1,seq->seq,model->ncycle,model->chol1,intensities);
        loglike = likelihood_cycle_intensities(simopt->sdfact,simopt->mu,lambda1,intensities,model->invchol1,loglike);
        uint32_t x = (uint32_t)( 1794 * runif());
        uint32_t y = (uint32_t)( 2048 * runif());

        if(NULL!=fpout){
            fprintf(fpout,"%u\t%u\t%u\t%u",model->lane,model->tile,x,y);
            fprint_intensities(fpout,"",intensities,false);
        }

        fprintf(stdout,"%u\t%u\t%u\t%u",model->lane,model->tile,x,y);
        if ( number_inpure_cycles(intensities,simopt->purity_threshold,simopt->purity_cycles) <= simopt->purity_max){
            fprint_intensities(stdout,"",loglike,false);
            calls = call_by_maximum_likelihood(loglike,calls);
            for ( uint32_t i=0 ; i<model->ncycle ; i++){
                if(calls[i] != seq->seq.elt[i]){ error[i]++;}
            }
            if ( model->paired ){
                lambda2 = rweibull(model->shape,model->scale);
                ARRAY(NUC) rcseq = reverse_complement(seq->seq);
                intensities = generate_pure_intensities(simopt->sdfact,lambda2,rcseq,model->ncycle,model->chol2,intensities);
                loglike = likelihood_cycle_intensities(simopt->sdfact,simopt->mu,lambda2,intensities,model->invchol2,loglike);
                if(NULL!=fpout){ fprint_intensities(fpout,"",intensities,false); }
                fprint_intensities(stdout,"",loglike,false);
                calls = call_by_maximum_likelihood(loglike,calls);
                for ( uint32_t i=0 ; i<model->ncycle ; i++){
                    if(calls[i] != rcseq.elt[i]){ error2[i]++;}
                }
                free_ARRAY(NUC)(rcseq);
            }
            unfiltered_count++;
        }
        fputc('\n',stdout);
        if(NULL!=fpout){ fputc('\n',fpout); }
        free_SEQ(seq);
        seq_count++;
        if( (seq_count%1000)==0 ){ fprintf(stderr,"\rDone: %8u",seq_count); }
    }
    fprintf(stderr,"\rFinished generating %8u sequences\n",seq_count);
    if(simopt->purity_cycles>0){ fprintf(stderr,"%8u sequences passed filter.\n",unfiltered_count);}
    if(NULL!=fpout){fclose(fpout);}
    safe_free(calls);
    free_MAT(loglike);
    free_MAT(intensities);

    // Print error summary
    fputs("Summary of errors, calling by maximum likelihood\n",stderr);
    fputs("Cycle  Count  Phred   lower, upper",stderr);
    if(simopt->paired){ fputs("   Count  Phred   lower, upper",stderr);}
    for ( uint32_t i=0 ; i<model->ncycle ; i++){
        const real_t e = ((real_t)error[i])/unfiltered_count;
	fprintf(stderr,"\n%3u: %7u %6.2f (%6.2f,%6.2f)",i+1,error[i], phred(e), phred(prop_upper(e,unfiltered_count)), phred(prop_lower(e,unfiltered_count)));
        if(simopt->paired){ 
            const real_t e2 = ((real_t)error2[i])/unfiltered_count;
            fprintf(stderr,"%7u %6.2f (%6.2f,%6.2f)",error2[i], phred(e2), phred(prop_upper(e2,unfiltered_count)), phred(prop_lower(e2,unfiltered_count)));
        }
    }
    fputc('\n',stderr);
    free_MODEL(model);
    free_SIMOPT(simopt);

    return EXIT_SUCCESS;
}

