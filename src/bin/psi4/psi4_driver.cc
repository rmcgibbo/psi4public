/*! \file psi4_driver.cc
\defgroup PSI4 The new PSI4 driver.
*/
#include "mpi.h"
#include <iostream>
#include <fstream>              // file I/O support
#include "libipv1/ip_lib.h"
#include <psi4-dec.h>
#include <string.h>
#include <algorithm>
#include <ctype.h>
#include "psi4.h"

namespace psi {

extern FILE *infile;
extern void setup_driver(Options &options);
extern int read_options(const std::string &jobName, Options &options);
extern int myid;

void psiclean(void);

int
psi4_driver(Options & options, int argc, char *argv[])
{
    // Initialize the list of function pointers for each module
    setup_driver(options);
    
    if (options.get_str("DERTYPE") == "NONE")
        options.set_str("DERTYPE", "ENERGY");

    // Track down the psi.dat file and set up ipv1 to read it
    // unless the user provided an exec array in the input
    std::string psiDatDirName;
    std::string psiDatFileName;
    if (getenv("PSIDATADIR"))
        psiDatDirName = getenv("PSIDATADIR");
    if (!psiDatDirName.empty()) {
        psiDatFileName = psiDatDirName;
    }else{
        psiDatFileName = INSTALLEDPSIDATADIR;
    }
    psiDatFileName += "/psi.dat";
    FILE * psidat = fopen(psiDatFileName.c_str(), "r");
    if(psidat == NULL){
        throw PsiException("Psi 4 could not locate psi.dat",
                           __FILE__, __LINE__);
    }
    ip_set_uppercase(1);
    ip_append(psidat, stdout);
    ip_cwk_clear();
    ip_cwk_add(const_cast<char*>(":DEFAULT"));
    ip_cwk_add(const_cast<char*>(":PSI"));
    fclose(psidat);

    // Join the job descriptors into one label
    std::string calcType = options.get_str("WFN");
    calcType += ":";
    calcType += options.get_str("DERTYPE");

    if(myid == 0) {
      std::string wfn = options.get_str("WFN");
      std::string reference = options.get_str("REFERENCE");
      std::string jobtype = options.get_str("JOBTYPE");
      std::string dertype = options.get_str("DERTYPE");
      fprintf(outfile, "    Calculation type = %s\n",calcType.c_str()); fflush(outfile);
      fprintf(outfile, "    Wavefunction     = %s\n",wfn.c_str()); fflush(outfile);
      fprintf(outfile, "    Reference        = %s\n",reference.c_str()); fflush(outfile);
      fprintf(outfile, "    Job type         = %s\n",jobtype.c_str()); fflush(outfile);
      fprintf(outfile, "    Derivative type  = %s\n", dertype.c_str()); fflush(outfile);
    }

    if(check_only)
      fprintf(outfile, "\n    Sanity check requested. Exiting without execution.\n\n");

    char *jobList = const_cast<char*>(calcType.c_str());
    // This version assumes that the array contains only module names, not
    // macros for other job types, like $SCF
    int numTasks = 0;
    int errcod;
    if(ip_exist(const_cast<char*>("EXEC"), 0)){
        // Override psi.dat with the commands in the exec array in input
        errcod = ip_count(const_cast<char*>("EXEC"), &numTasks, 0);
        jobList = const_cast<char*>("EXEC");
    }else{
        errcod = ip_count(jobList, &numTasks, 0);
        if (!ip_exist(jobList, 0)){
            std::string err("Error: jobtype ");
            err += jobList;
            err += " is not defined in psi.dat";
            throw PsiException(err, __FILE__, __LINE__);
        }
        if (errcod != IPE_OK){
            std::string err("Error: trouble reading ");
            err += jobList;
            err += " array from psi.dat";
            throw PsiException(err, __FILE__, __LINE__);
        }
    }

    if(myid == 0) fprintf(outfile, "    List of tasks to execute:\n");
    for(int n = 0; n < numTasks; ++n) {
        char *thisJob;
        ip_string(jobList, &thisJob, 1, n);
        if(myid == 0) fprintf(outfile, "    %s\n", thisJob);
        free(thisJob);
    }

    if(check_only) return Success;

    for(int n = 0; n < numTasks; ++n){
        char *thisJob;
        errcod = ip_string(jobList, &thisJob, 1, n);
        // Make sure the job name is all upper case
        int length = strlen(thisJob);
        std::transform(thisJob, thisJob + length, thisJob, ::toupper);
        if(myid == 0)
          fprintf(outfile, "Job %d is %s\n", n, thisJob); fflush(outfile);
        read_options(thisJob, options);

        if(dispatch_table.find(thisJob) == dispatch_table.end()){
            std::string err = "Module ";
            err += thisJob;
            err += " is not known to PSI4.  Please update the driver\n";
            throw PsiException(err, __FILE__, __LINE__);
        }

        // If the function call is LMP2, run in parallel
        if(strcmp(thisJob, "LMP2") == 0) {
            // Needed a barrier before the functions are called
            MPI_Barrier(MPI_COMM_WORLD);
            if (dispatch_table[thisJob](options, argc, argv) != Success) {
                // Good chance at this time that an error occurred.
                // Report it to the user.
                fprintf(stderr, "%s did not return a Success code.\n", thisJob);
                throw PsiException("Module failed.", __FILE__, __LINE__);
            }
        }
        // If any other functions are called only process 0 runs the function
        else {
            // Needed a barrier before the functions are called
            MPI_Barrier(MPI_COMM_WORLD);
            if(myid == 0) {
                if (dispatch_table[thisJob](options, argc, argv) != Success) {
                // Good chance at this time that an error occurred.
                // Report it to the user.
                    fprintf(stderr, "%s did not return a Success code.\n", thisJob);
                    throw PsiException("Module failed.", __FILE__, __LINE__);
                }
            }
        }

        fflush(outfile);
        free(thisJob);
    }

    psiclean();
    
    return Success;
}

} // Namespace
