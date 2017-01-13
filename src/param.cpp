/*
    Genome-wide Efficient Mixed Model Association (GEMMA)
    Copyright (C) 2011  Xiang Zhou

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <sys/stat.h>
#include <cmath>
#include <algorithm>

#include "gsl/gsl_randist.h"
#include "gsl/gsl_matrix.h"
#include "gsl/gsl_vector.h"
#include "gsl/gsl_matrix.h"
#include "gsl/gsl_linalg.h"
#include "gsl/gsl_blas.h"

#include "eigenlib.h"
#include "mathfunc.h"

#ifdef FORCE_FLOAT
#include "param_float.h"
#include "io_float.h"
#else
#include "param.h"
#include "io.h"
#endif

using namespace std;





PARAM::PARAM(void):
mode_silence (false), a_mode (0), k_mode(1), d_pace (100000),
file_out("result"), path_out("./output/"),
miss_level(0.05), maf_level(0.01), hwe_level(0), r2_level(0.9999),
l_min(1e-5), l_max(1e5), n_region(10),p_nr(0.001),em_prec(0.0001),nr_prec(0.0001),em_iter(10000),nr_iter(100),crt(0),
pheno_mean(0), noconstrain (false),
h_min(-1), h_max(-1),	h_scale(-1),
rho_min(0.0), rho_max(1.0),	rho_scale(-1),
logp_min(0.0), logp_max(0.0), logp_scale(-1),
s_min(0), s_max(300),
w_step(100000),	s_step(1000000),
r_pace(10), w_pace(1000),
n_accept(0),
n_mh(10),
geo_mean(2000.0),
randseed(-1),
window_cm(0), window_bp(0), window_ns(0), n_block(200),
error(false),
ni_subsample(0), n_cvt(1), n_vc(1),
time_total(0.0), time_G(0.0), time_eigen(0.0), time_UtX(0.0), time_UtZ(0.0), time_opt(0.0), time_Omega(0.0)
{}


//read files
//obtain ns_total, ng_total, ns_test, ni_test
void PARAM::ReadFiles (void)
{
	string file_str;

	//read cat file
	if (!file_mcat.empty()) {
	  if (ReadFile_mcat (file_mcat, mapRS2cat, n_vc)==false) {error=true;}
	} else if (!file_cat.empty()) {
	  if (ReadFile_cat (file_cat, mapRS2cat, n_vc)==false) {error=true;}
	}

	//read snp weight files
	if (!file_wcat.empty()) {
	  if (ReadFile_wsnp (file_wcat, n_vc, mapRS2wcat)==false) {error=true;}
	}
	if (!file_wsnp.empty()) {
	  if (ReadFile_wsnp (file_wsnp, mapRS2wsnp)==false) {error=true;}
	}

	//count number of kinship files
	if (!file_mk.empty()) {
	  if (CountFileLines (file_mk, n_vc)==false) {error=true;}
	}

	//read snp set
	if (!file_snps.empty()) {
		if (ReadFile_snps (file_snps, setSnps)==false) {error=true;}
	} else {
		setSnps.clear();
	}

	//for prediction
	if (!file_epm.empty()) {
		if (ReadFile_est (file_epm, est_column, mapRS2est)==false) {error=true;}

		if (!file_bfile.empty()) {
			file_str=file_bfile+".bim";
			if (ReadFile_bim (file_str, snpInfo)==false) {error=true;}

			file_str=file_bfile+".fam";
			if (ReadFile_fam (file_str, indicator_pheno, pheno, mapID2num, p_column)==false) {error=true;}
		}

		if (!file_geno.empty()) {
			if (ReadFile_pheno (file_pheno, indicator_pheno, pheno, p_column)==false) {error=true;}

			if (CountFileLines (file_geno, ns_total)==false) {error=true;}
		}

		if (!file_ebv.empty() ) {
			if (ReadFile_column (file_ebv, indicator_bv, vec_bv, 1)==false) {error=true;}
		}

		if (!file_log.empty() ) {
			if (ReadFile_log (file_log, pheno_mean)==false) {error=true;}
		}

		//convert indicator_pheno to indicator_idv
		int k=1;
		for (size_t i=0; i<indicator_pheno.size(); i++) {
			k=1;
			for (size_t j=0; j<indicator_pheno[i].size(); j++) {
				if (indicator_pheno[i][j]==0) {k=0;}
			}
			indicator_idv.push_back(k);
		}

		ns_test=0;

		return;
	}

	//read covariates before the genotype files
	if (!file_cvt.empty() ) {
		if (ReadFile_cvt (file_cvt, indicator_cvt, cvt, n_cvt)==false) {error=true;}

		if ((indicator_cvt).size()==0) {
			n_cvt=1;
		}
	} else {
		n_cvt=1;
	}

	if (!file_gxe.empty() ) {
	  if (ReadFile_column (file_gxe, indicator_gxe, gxe, 1)==false) {error=true;}
	}
	if (!file_weight.empty() ) {
	  if (ReadFile_column (file_weight, indicator_weight, weight, 1)==false) {error=true;}
	}


	// WJA added
	//read genotype and phenotype file for bgen format
	if (!file_oxford.empty()) {
		file_str=file_oxford+".sample";
		if (ReadFile_sample(file_str, indicator_pheno, pheno, p_column,indicator_cvt, cvt, n_cvt)==false) {error=true;}
		if ((indicator_cvt).size()==0) {
			n_cvt=1;
		}
	//	n_cvt=1;

		//post-process covariates and phenotypes, obtain ni_test, save all useful covariates
		ProcessCvtPhen();

		//obtain covariate matrix
		gsl_matrix *W=gsl_matrix_alloc (ni_test, n_cvt);
		CopyCvt (W);

		file_str=file_oxford+".bgen";
		if (ReadFile_bgen (file_str, setSnps, W, indicator_idv, indicator_snp, snpInfo, maf_level, miss_level, hwe_level, r2_level, ns_test)==false) {error=true;}
		gsl_matrix_free(W);

		ns_total=indicator_snp.size();
	}


	//read genotype and phenotype file for plink format
	if (!file_bfile.empty()) {
		file_str=file_bfile+".bim";
		snpInfo.clear();
		if (ReadFile_bim (file_str, snpInfo)==false) {error=true;}

		//if both fam file and pheno files are used, use phenotypes inside the pheno file
		if (!file_pheno.empty()) {
		  //phenotype file before genotype file
		  if (ReadFile_pheno (file_pheno, indicator_pheno, pheno, p_column)==false) {error=true;}
		} else {
		  file_str=file_bfile+".fam";
		  if (ReadFile_fam (file_str, indicator_pheno, pheno, mapID2num, p_column)==false) {error=true;}
		}

		//post-process covariates and phenotypes, obtain ni_test, save all useful covariates
		ProcessCvtPhen();

		//obtain covariate matrix
		gsl_matrix *W=gsl_matrix_alloc (ni_test, n_cvt);
		CopyCvt (W);

		file_str=file_bfile+".bed";
		if (ReadFile_bed (file_str, setSnps, W, indicator_idv, indicator_snp, snpInfo, maf_level, miss_level, hwe_level, r2_level, ns_test)==false) {error=true;}

		gsl_matrix_free(W);

		ns_total=indicator_snp.size();
	}

	//read genotype and phenotype file for bimbam format
	if (!file_geno.empty()) {
		//annotation file before genotype file
		if (!file_anno.empty() ) {
			if (ReadFile_anno (file_anno, mapRS2chr, mapRS2bp, mapRS2cM)==false) {error=true;}
		}

		//phenotype file before genotype file
		if (ReadFile_pheno (file_pheno, indicator_pheno, pheno, p_column)==false) {error=true;}

		//post-process covariates and phenotypes, obtain ni_test, save all useful covariates
		ProcessCvtPhen();

		//obtain covariate matrix
		gsl_matrix *W=gsl_matrix_alloc (ni_test, n_cvt);
		CopyCvt (W);

		if (ReadFile_geno (file_geno, setSnps, W, indicator_idv, indicator_snp, maf_level, miss_level, hwe_level, r2_level, mapRS2chr, mapRS2bp, mapRS2cM, snpInfo, ns_test)==false) {error=true;}

		gsl_matrix_free(W);

		ns_total=indicator_snp.size();
	}


	//read genotype file for multiple plink files
	if (!file_mbfile.empty()) {
	  igzstream infile (file_mbfile.c_str(), igzstream::in);
	  if (!infile) {cout<<"error! fail to open mbfile file: "<<file_mbfile<<endl; return;}

	  string file_name;

	  size_t t=0, ns_test_tmp=0;

	  gsl_matrix *W;
	  while (!safeGetline(infile, file_name).eof()) {
		file_str=file_name+".bim";

		if (ReadFile_bim (file_str, snpInfo)==false) {error=true;}

		if (t==0) {
		  //if both fam file and pheno files are used, use phenotypes inside the pheno file
		  if (!file_pheno.empty()) {
		    //phenotype file before genotype file
		    if (ReadFile_pheno (file_pheno, indicator_pheno, pheno, p_column)==false) {error=true;}
		  } else {
		    file_str=file_name+".fam";
		    if (ReadFile_fam (file_str, indicator_pheno, pheno, mapID2num, p_column)==false) {error=true;}
		  }

		  //post-process covariates and phenotypes, obtain ni_test, save all useful covariates
		  ProcessCvtPhen();

		  //obtain covariate matrix
		  W=gsl_matrix_alloc (ni_test, n_cvt);
		  CopyCvt (W);
		}

		file_str=file_name+".bed";
		if (ReadFile_bed (file_str, setSnps, W, indicator_idv, indicator_snp, snpInfo, maf_level, miss_level, hwe_level, r2_level, ns_test_tmp)==false) {error=true;}
		mindicator_snp.push_back(indicator_snp);
		msnpInfo.push_back(snpInfo);
		ns_test+=ns_test_tmp;
		ns_total+=indicator_snp.size();

		t++;
	  }

	  gsl_matrix_free(W);

	  infile.close();
	  infile.clear();
	}



	//read genotype and phenotype file for multiple bimbam files
	if (!file_mgeno.empty()) {
	  //annotation file before genotype file
	  if (!file_anno.empty() ) {
	    if (ReadFile_anno (file_anno, mapRS2chr, mapRS2bp, mapRS2cM)==false) {error=true;}
	  }

	  //phenotype file before genotype file
	  if (ReadFile_pheno (file_pheno, indicator_pheno, pheno, p_column)==false) {error=true;}

	  //post-process covariates and phenotypes, obtain ni_test, save all useful covariates
	  ProcessCvtPhen();

	  //obtain covariate matrix
	  gsl_matrix *W=gsl_matrix_alloc (ni_test, n_cvt);
	  CopyCvt (W);

	  igzstream infile (file_mgeno.c_str(), igzstream::in);
	  if (!infile) {cout<<"error! fail to open mgeno file: "<<file_mgeno<<endl; return;}

	  string file_name;
	  size_t ns_test_tmp;
	  while (!safeGetline(infile, file_name).eof()) {
	    if (ReadFile_geno (file_name, setSnps, W, indicator_idv, indicator_snp, maf_level, miss_level, hwe_level, r2_level, mapRS2chr, mapRS2bp, mapRS2cM, snpInfo, ns_test_tmp)==false) {error=true;}

	    mindicator_snp.push_back(indicator_snp);
	    msnpInfo.push_back(snpInfo);
	    ns_test+=ns_test_tmp;
	    ns_total+=indicator_snp.size();
	  }

	  gsl_matrix_free(W);

	  infile.close();
	  infile.clear();
	}



	if (!file_gene.empty()) {
		if (ReadFile_pheno (file_pheno, indicator_pheno, pheno, p_column)==false) {error=true;}

		//convert indicator_pheno to indicator_idv
		int k=1;
		for (size_t i=0; i<indicator_pheno.size(); i++) {
			k=1;
			for (size_t j=0; j<indicator_pheno[i].size(); j++) {
				if (indicator_pheno[i][j]==0) {k=0;}
			}
			indicator_idv.push_back(k);
		}

		//post-process covariates and phenotypes, obtain ni_test, save all useful covariates
		ProcessCvtPhen();

		//obtain covariate matrix
		gsl_matrix *W=gsl_matrix_alloc (ni_test, n_cvt);
		CopyCvt (W);

		if (ReadFile_gene (file_gene, vec_read, snpInfo, ng_total)==false) {error=true;}
	}


	//read is after gene file
	if (!file_read.empty() ) {
		if (ReadFile_column (file_read, indicator_read, vec_read, 1)==false) {error=true;}

		ni_test=0;
		for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
			indicator_idv[i]*=indicator_read[i];
			ni_test+=indicator_idv[i];
		}

		if (ni_test==0) {
			error=true;
			cout<<"error! number of analyzed individuals equals 0. "<<endl;
			return;
		}
	}

	//for ridge prediction, read phenotype only
	if (file_geno.empty() && file_gene.empty() && !file_pheno.empty()) {
		if (ReadFile_pheno (file_pheno, indicator_pheno, pheno, p_column)==false) {error=true;}

		//post-process covariates and phenotypes, obtain ni_test, save all useful covariates
		ProcessCvtPhen();
	}

	return;
}






void PARAM::CheckParam (void)
{
	struct stat fileInfo;
	string str;

	//check parameters
	if (k_mode!=1 && k_mode!=2) {cout<<"error! unknown kinship/relatedness input mode: "<<k_mode<<endl; error=true;}
	if (a_mode!=1 && a_mode!=2 && a_mode!=3 && a_mode!=4 && a_mode!=5 && a_mode!=11 && a_mode!=12 && a_mode!=13 && a_mode!=14 && a_mode!=21 && a_mode!=22 && a_mode!=25 && a_mode!=26 && a_mode!=27 && a_mode!=28 && a_mode!=31 && a_mode!=41 && a_mode!=42 && a_mode!=43 && a_mode!=51 && a_mode!=52 && a_mode!=53 && a_mode!=54 && a_mode!=61 && a_mode!=62 && a_mode!=66 && a_mode!=67 && a_mode!=71)
	{cout<<"error! unknown analysis mode: "<<a_mode<<". make sure -gk or -eigen or -lmm or -bslmm -predict or -calccov is sepcified correctly."<<endl; error=true;}
	if (miss_level>1) {cout<<"error! missing level needs to be between 0 and 1. current value = "<<miss_level<<endl; error=true;}
	if (maf_level>0.5) {cout<<"error! maf level needs to be between 0 and 0.5. current value = "<<maf_level<<endl; error=true;}
	if (hwe_level>1) {cout<<"error! hwe level needs to be between 0 and 1. current value = "<<hwe_level<<endl; error=true;}
	if (r2_level>1) {cout<<"error! r2 level needs to be between 0 and 1. current value = "<<r2_level<<endl; error=true;}

	if (l_max<l_min) {cout<<"error! maximum lambda value must be larger than the minimal value. current values = "<<l_max<<" and "<<l_min<<endl; error=true;}
	if (h_max<h_min) {cout<<"error! maximum h value must be larger than the minimal value. current values = "<<h_max<<" and "<<h_min<<endl; error=true;}
	if (s_max<s_min) {cout<<"error! maximum s value must be larger than the minimal value. current values = "<<s_max<<" and "<<s_min<<endl; error=true;}
	if (rho_max<rho_min) {cout<<"error! maximum rho value must be larger than the minimal value. current values = "<<rho_max<<" and "<<rho_min<<endl; error=true;}
	if (logp_max<logp_min) {cout<<"error! maximum logp value must be larger than the minimal value. current values = "<<logp_max/log(10)<<" and "<<logp_min/log(10)<<endl; error=true;}

	if (h_max>1) {cout<<"error! h values must be bewtween 0 and 1. current values = "<<h_max<<" and "<<h_min<<endl; error=true;}
	if (rho_max>1) {cout<<"error! rho values must be between 0 and 1. current values = "<<rho_max<<" and "<<rho_min<<endl; error=true;}
	if (logp_max>0) {cout<<"error! maximum logp value must be smaller than 0. current values = "<<logp_max/log(10)<<" and "<<logp_min/log(10)<<endl; error=true;}
	if (l_max<l_min) {cout<<"error! maximum lambda value must be larger than the minimal value. current values = "<<l_max<<" and "<<l_min<<endl; error=true;}

	if (h_scale>1.0) {cout<<"error! hscale value must be between 0 and 1. current value = "<<h_scale<<endl; error=true;}
	if (rho_scale>1.0) {cout<<"error! rscale value must be between 0 and 1. current value = "<<rho_scale<<endl; error=true;}
	if (logp_scale>1.0) {cout<<"error! pscale value must be between 0 and 1. current value = "<<logp_scale<<endl; error=true;}

	if (rho_max==1 && rho_min==1 && a_mode==12) {cout<<"error! ridge regression does not support a rho parameter. current values = "<<rho_max<<" and "<<rho_min<<endl; error=true;}

	if (window_cm<0) {cout<<"error! windowcm values must be non-negative. current values = "<<window_cm<<endl; error=true;}

	if (window_cm==0 && window_bp==0 && window_ns==0) {
	  window_bp=1000000;
	}

	//check p_column, and (no need to) sort p_column into ascending order
	if (p_column.size()==0) {
		p_column.push_back(1);
	} else {
		for (size_t i=0; i<p_column.size(); i++) {
			for (size_t j=0; j<i; j++) {
				if (p_column[i]==p_column[j]) {cout<<"error! identical phenotype columns: "<<p_column[i]<<endl; error=true;}
			}
		}
	}

	//sort (p_column.begin(), p_column.end() );
	n_ph=p_column.size();



	//only lmm option (and one prediction option) can deal with multiple phenotypes
	//and no gene expression files
	if (n_ph>1 && a_mode!=1 && a_mode!=2 && a_mode!=3 && a_mode!=4 && a_mode!=43) {
		cout<<"error! the current analysis mode "<<a_mode<<" can not deal with multiple phenotypes."<<endl; error=true;
	}
	if (n_ph>1 && !file_gene.empty() ) {
		cout<<"error! multiple phenotype analysis option not allowed with gene expression files. "<<endl; error=true;
	}

	if (p_nr>1) {
		cout<<"error! pnr value must be between 0 and 1. current value = "<<p_nr<<endl; error=true;
	}

	//check est_column
	if (est_column.size()==0) {
		if (file_ebv.empty()) {
			est_column.push_back(2);
			est_column.push_back(5);
			est_column.push_back(6);
			est_column.push_back(7);
		} else {
			est_column.push_back(2);
			est_column.push_back(0);
			est_column.push_back(6);
			est_column.push_back(7);
		}
	}

	if (est_column.size()!=4) {cout<<"error! -en not followed by four numbers. current number = "<<est_column.size()<<endl; error=true;}
	if (est_column[0]==0) {cout<<"error! -en rs column can not be zero. current number = "<<est_column.size()<<endl; error=true;}

	//check if files are compatible with each other, and if files exist
	if (!file_bfile.empty()) {
		str=file_bfile+".bim";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .bim file: "<<str<<endl; error=true;}
		str=file_bfile+".bed";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .bed file: "<<str<<endl; error=true;}
		str=file_bfile+".fam";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .fam file: "<<str<<endl; error=true;}
	}

	if (!file_oxford.empty()) {
		str=file_oxford+".bgen";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .bgen file: "<<str<<endl; error=true;}
		str=file_oxford+".sample";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .sample file: "<<str<<endl; error=true;}
	}

	if ((!file_geno.empty() || !file_gene.empty()) ) {
		str=file_pheno;
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open phenotype file: "<<str<<endl; error=true;}
	}

	str=file_geno;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open mean genotype file: "<<str<<endl; error=true;}

	str=file_gene;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open gene expression file: "<<str<<endl; error=true;}

	str=file_cat;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open category file: "<<str<<endl; error=true;}

	str=file_mcat;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open mcategory file: "<<str<<endl; error=true;}

	str=file_beta;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open beta file: "<<str<<endl; error=true;}

	str=file_cor;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open correlation file: "<<str<<endl; error=true;}

	if (!file_study.empty()) {
	  str=file_study+".Vq.txt";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .Vq.txt file: "<<str<<endl; error=true;}
		str=file_study+".q.txt";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .q.txt file: "<<str<<endl; error=true;}
		str=file_study+".size.txt";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .size.txt file: "<<str<<endl; error=true;}
	}

	if (!file_ref.empty()) {
		str=file_ref+".S.txt";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .S.txt file: "<<str<<endl; error=true;}
		str=file_ref+".size.txt";
		if (stat(str.c_str(),&fileInfo)==-1) {cout<<"error! fail to open .size.txt file: "<<str<<endl; error=true;}
	}

	str=file_mstudy;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open mstudy file: "<<str<<endl; error=true;}

	str=file_mref;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open mref file: "<<str<<endl; error=true;}

	str=file_mgeno;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open mgeno file: "<<str<<endl; error=true;}

	str=file_mbfile;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open mbfile file: "<<str<<endl; error=true;}

	size_t flag=0;
	if (!file_bfile.empty()) {flag++;}
	if (!file_geno.empty()) {flag++;}
	if (!file_gene.empty()) {flag++;}
	// WJA added
	if (!file_oxford.empty()) {flag++;}

	if (flag!=1 && a_mode!=27 && a_mode!=28 && a_mode!=43 && a_mode!=5 && a_mode!=61 && a_mode!=62 && a_mode!=66 && a_mode!=67) {
		cout<<"error! either plink binary files, or bimbam mean genotype files, or gene expression files are required."<<endl; error=true;
	}

	if (file_pheno.empty() && (a_mode==43 || a_mode==5) ) {
		cout<<"error! phenotype file is required."<<endl; error=true;
	}

	if (a_mode==61 || a_mode==62) {
	  if (!file_beta.empty()) {
	    if ( file_mbfile.empty() && file_bfile.empty() && file_mgeno.empty() && file_geno.empty() && file_mref.empty() && file_ref.empty() ) {
	      cout<<"error! missing genotype file or ref/mref file."<<endl; error=true;
	    }
	  } else if (!file_pheno.empty()) {
	    if (file_kin.empty() && (file_ku.empty()||file_kd.empty()) && file_mk.empty() ) {
	      cout<<"error! missing relatedness file. "<<endl;  error=true;
	    }
	    /*
	  } else if (!file_cor.empty()) {
	    if (file_beta.empty() ) {
	      cout<<"error! missing cor file."<<endl; error=true;
	    }
	    */
	  } else if ( (file_mstudy.empty() && file_study.empty()) || (file_mref.empty() && file_ref.empty() )  ) {
	      cout<<"error! either beta file, or phenotype files or study/ref mstudy/mref files are required."<<endl; error=true;
	  }
	}

	if (a_mode==66 || a_mode==67) {
	  if (file_beta.empty() || ( file_mbfile.empty() && file_bfile.empty() && file_mgeno.empty() && file_geno.empty()) ) {
	    cout<<"error! missing beta file or genotype file."<<endl; error=true;
	  }
	}


	if (!file_epm.empty() && file_bfile.empty() && file_geno.empty() ) {cout<<"error! estimated parameter file also requires genotype file."<<endl; error=true;}
	if (!file_ebv.empty() && file_kin.empty()) {cout<<"error! estimated breeding value file also requires relatedness file."<<endl; error=true;}

	if (!file_log.empty() && pheno_mean!=0) {cout<<"error! either log file or mu value can be provide."<<endl; error=true;}

	str=file_snps;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open snps file: "<<str<<endl; error=true;}

	str=file_log;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open log file: "<<str<<endl; error=true;}

	str=file_anno;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open annotation file: "<<str<<endl; error=true;}

	str=file_kin;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open relatedness matrix file: "<<str<<endl; error=true;}

	str=file_mk;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open relatedness matrix file: "<<str<<endl; error=true;}

	str=file_cvt;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open covariates file: "<<str<<endl; error=true;}

	str=file_gxe;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open environmental covariate file: "<<str<<endl; error=true;}

	str=file_weight;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open the residual weight file: "<<str<<endl; error=true;}

	str=file_epm;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open estimated parameter file: "<<str<<endl; error=true;}

	str=file_ebv;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open estimated breeding value file: "<<str<<endl; error=true;}

	str=file_read;
	if (!str.empty() && stat(str.c_str(),&fileInfo)==-1 ) {cout<<"error! fail to open total read file: "<<str<<endl; error=true;}

	//check if files are compatible with analysis mode
	if (k_mode==2 && !file_geno.empty() ) {cout<<"error! use \"-km 1\" when using bimbam mean genotype file. "<<endl; error=true;}

	if ((a_mode==1 || a_mode==2 || a_mode==3 || a_mode==4 || a_mode==5 || a_mode==31) && (file_kin.empty() && (file_ku.empty()||file_kd.empty())) )  {cout<<"error! missing relatedness file. "<<endl;  error=true;}

	if ((a_mode==43) && file_kin.empty())  {cout<<"error! missing relatedness file. -predict option requires -k option to provide a relatedness file."<<endl;  error=true;}

	if ((a_mode==11 || a_mode==12 || a_mode==13) && !file_cvt.empty() ) {cout<<"error! -bslmm option does not support covariates files."<<endl; error=true;}

	if (a_mode==41 || a_mode==42) {
		if (!file_cvt.empty() ) {cout<<"error! -predict option does not support covariates files."<<endl; error=true;}
		if (file_epm.empty() ) {cout<<"error! -predict option requires estimated parameter files."<<endl; error=true;}
	}

	if (file_beta.empty() && (a_mode==27 || a_mode==28) ) {
		cout<<"error! beta effects file is required."<<endl; error=true;
	}

	return;
}





void PARAM::CheckData (void) {
  if(file_oxford.empty())	// WJA NOTE: I added this condition so that covariates can be added through sample, probably not exactly what is wanted
	{
 	if ((file_cvt).empty() || (indicator_cvt).size()==0) {
 		n_cvt=1;
 	}
	}

  if ( (a_mode==66 || a_mode==67) && (v_pve.size()!=n_vc))  {
	    cout<<"error! the number of pve estimates does not equal to the number of categories in the cat file:"<<v_pve.size()<<" "<<n_vc<<endl; error=true;
	}

	if ( (indicator_cvt).size()!=0 && (indicator_cvt).size()!=(indicator_idv).size()) {
		error=true;
		cout<<"error! number of rows in the covariates file do not match the number of individuals. "<<endl;
		return;
	}
	if ( (indicator_gxe).size()!=0 && (indicator_gxe).size()!=(indicator_idv).size()) {
		error=true;
		cout<<"error! number of rows in the gxe file do not match the number of individuals. "<<endl;
		return;
	}
	if ( (indicator_weight).size()!=0 && (indicator_weight).size()!=(indicator_idv).size()) {
		error=true;
		cout<<"error! number of rows in the weight file do not match the number of individuals. "<<endl;
		return;
	}

	if ( (indicator_read).size()!=0 && (indicator_read).size()!=(indicator_idv).size()) {
		error=true;
		cout<<"error! number of rows in the total read file do not match the number of individuals. "<<endl;
		return;
	}

	//calculate ni_total and ni_test, and set indicator_idv to 0 whenever indicator_cvt=0
	//and calculate np_obs and np_miss
	ni_total=(indicator_idv).size();

	ni_test=0;
	for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
		if (indicator_idv[i]==0) {continue;}
		ni_test++;
	}

	ni_cvt=0;
	for (size_t i=0; i<indicator_cvt.size(); i++) {
		if (indicator_cvt[i]==0) {continue;}
		ni_cvt++;
	}

	np_obs=0; np_miss=0;
	for (size_t i=0; i<indicator_pheno.size(); i++) {
		if (indicator_cvt.size()!=0) {
			if (indicator_cvt[i]==0) {continue;}
		}

		if (indicator_gxe.size()!=0) {
			if (indicator_gxe[i]==0) {continue;}
		}

		if (indicator_weight.size()!=0) {
			if (indicator_weight[i]==0) {continue;}
		}

		for (size_t j=0; j<indicator_pheno[i].size(); j++) {
			if (indicator_pheno[i][j]==0) {
				np_miss++;
			} else {
				np_obs++;
			}
		}
	}

	/*
	if ((indicator_cvt).size()!=0) {
		ni_test=0;
		for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
			indicator_idv[i]*=indicator_cvt[i];
			ni_test+=indicator_idv[i];
		}
	}

	if ((indicator_read).size()!=0) {
		ni_test=0;
		for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
			indicator_idv[i]*=indicator_read[i];
			ni_test+=indicator_idv[i];
		}
	}
	*/
	if (ni_test==0 && file_cor.empty() && file_mstudy.empty() && file_study.empty() && file_beta.empty() ) {
		error=true;
		cout<<"error! number of analyzed individuals equals 0. "<<endl;
		return;
	}

	if (a_mode==43) {
		if (ni_cvt==ni_test) {
			error=true;
			cout<<"error! no individual has missing phenotypes."<<endl;
			return;
		}
		if ((np_obs+np_miss)!=(ni_cvt*n_ph)) {
			error=true;
			//cout<<ni_cvt<<"\t"<<ni_test<<"\t"<<ni_total<<"\t"<<np_obs<<"\t"<<np_miss<<"\t"<<indicator_cvt.size()<<endl;
			cout<<"error! number of phenotypes do not match the summation of missing and observed phenotypes."<<endl;
			return;
		}
	}

	//output some information
	if (file_cor.empty() && file_mstudy.empty() && file_study.empty() && a_mode!=27 && a_mode!=28) {
	  cout<<"## number of total individuals = "<<ni_total<<endl;
	  if (a_mode==43) {
	    cout<<"## number of analyzed individuals = "<<ni_cvt<<endl;
	    cout<<"## number of individuals with full phenotypes = "<<ni_test<<endl;
	  } else {
	    cout<<"## number of analyzed individuals = "<<ni_test<<endl;
	  }
	  cout<<"## number of covariates = "<<n_cvt<<endl;
	  cout<<"## number of phenotypes = "<<n_ph<<endl;
	  if (a_mode==43) {
	    cout<<"## number of observed data = "<<np_obs<<endl;
	    cout<<"## number of missing data = "<<np_miss<<endl;
	  }
	  if (!file_gene.empty()) {
	    cout<<"## number of total genes = "<<ng_total<<endl;
	  } else if (file_epm.empty() && a_mode!=43 && a_mode!=5) {
	    cout<<"## number of total SNPs = "<<ns_total<<endl;
	    cout<<"## number of analyzed SNPs = "<<ns_test<<endl;
	  } else {}
	}

	//set d_pace to 1000 for gene expression
	if (!file_gene.empty() && d_pace==100000) {
		d_pace=1000;
	}

	//for case-control studies, count #cases and #controls
	int flag_cc=0;
	if (a_mode==13) {
		ni_case=0;
		ni_control=0;
		for (size_t i=0; i<indicator_idv.size(); i++) {
			if (indicator_idv[i]==0) {continue;}

			if (pheno[i][0]==0) {ni_control++;}
			else if (pheno[i][0]==1) {ni_case++;}
			else {flag_cc=1;}
		}
		cout<<"## number of cases = "<<ni_case<<endl;
		cout<<"## number of controls = "<<ni_control<<endl;
	}

	if (flag_cc==1) {cout<<"Unexpected non-binary phenotypes for case/control analysis. Use default (BSLMM) analysis instead."<<endl; a_mode=11;}

	//set parameters for BSLMM
	//and check for predict
	if (a_mode==11 || a_mode==12 || a_mode==13) {
		if (a_mode==11) {n_mh=1;}
		if (logp_min==0) {logp_min=-1.0*log((double)ns_test);}

		if (h_scale==-1) {h_scale=min(1.0, 10.0/sqrt((double)ni_test) );}
		if (rho_scale==-1) {rho_scale=min(1.0, 10.0/sqrt((double)ni_test) );}
		if (logp_scale==-1) {logp_scale=min(1.0, 5.0/sqrt((double)ni_test) );}

		if (h_min==-1) {h_min=0.0;}
		if (h_max==-1) {h_max=1.0;}

		if (s_max>ns_test) {s_max=ns_test; cout<<"s_max is re-set to the number of analyzed SNPs."<<endl;}
		if (s_max<s_min) {cout<<"error! maximum s value must be larger than the minimal value. current values = "<<s_max<<" and "<<s_min<<endl; error=true;}
	} else if (a_mode==41 || a_mode==42) {
		if (indicator_bv.size()!=0) {
			if (indicator_idv.size()!=indicator_bv.size()) {
				cout<<"error! number of rows in the phenotype file does not match that in the estimated breeding value file: "<<indicator_idv.size()<<"\t"<<indicator_bv.size()<<endl;
				error=true;
			} else {
				size_t flag_bv=0;
				for (size_t i=0; i<(indicator_bv).size(); ++i) {
					if (indicator_idv[i]!=indicator_bv[i]) {flag_bv++;}
				}
				if (flag_bv!=0) {
					cout<<"error! individuals with missing value in the phenotype file does not match that in the estimated breeding value file: "<<flag_bv<<endl;
					error=true;
				}
			}
		}
	}

	if (a_mode==62 && !file_beta.empty() && mapRS2wcat.size()==0) {cout<<"vc analysis with beta files requires -wcat file."<<endl; error=true;}
	if (a_mode==67 && mapRS2wcat.size()==0) {cout<<"ci analysis with beta files requires -wcat file."<<endl; error=true;}

	//file_mk needs to contain more than one line
	if (n_vc==1 && !file_mk.empty()) {cout<<"error! -mk file should contain more than one line."<<endl; error=true;}

	return;
}


void PARAM::PrintSummary ()
{
	if (n_ph==1) {
		cout<<"pve estimate ="<<pve_null<<endl;
		cout<<"se(pve) ="<<pve_se_null<<endl;
	} else {

	}
	return;
}



void PARAM::ReadGenotypes (gsl_matrix *UtX, gsl_matrix *K, const bool calc_K) {
	string file_str;

	if (!file_bfile.empty()) {
		file_str=file_bfile+".bed";
		if (ReadFile_bed (file_str, indicator_idv, indicator_snp, UtX, K, calc_K)==false) {error=true;}
	}
	else {
		if (ReadFile_geno (file_geno, indicator_idv, indicator_snp, UtX, K, calc_K)==false) {error=true;}
	}

	return;
}


void PARAM::ReadGenotypes (vector<vector<unsigned char> > &Xt, gsl_matrix *K, const bool calc_K) {
	string file_str;

	if (!file_bfile.empty()) {
		file_str=file_bfile+".bed";
		if (ReadFile_bed (file_str, indicator_idv, indicator_snp, Xt, K, calc_K, ni_test, ns_test)==false) {error=true;}
	} else {
		if (ReadFile_geno (file_geno, indicator_idv, indicator_snp, Xt, K, calc_K, ni_test, ns_test)==false) {error=true;}
	}

	return;
}




void PARAM::CalcKin (gsl_matrix *matrix_kin)  {
	string file_str;

	gsl_matrix_set_zero (matrix_kin);

	if (!file_bfile.empty() ) {
		file_str=file_bfile+".bed";
		if (PlinkKin (file_str, indicator_snp, a_mode-20, d_pace, matrix_kin)==false) {error=true;}
	}
	else if (!file_oxford.empty() ) {
		file_str=file_oxford+".bgen";
		if (bgenKin (file_str, indicator_snp, a_mode-20, d_pace, matrix_kin)==false) {error=true;}
 	}
	else {
		file_str=file_geno;
		if (BimbamKin (file_str, indicator_snp, a_mode-20, d_pace, matrix_kin)==false) {error=true;}
	}

	return;
}



//from an existing n by nd A and K matrices, compute the d by d S matrix (which is not necessary symmetric)
void compAKtoS (const gsl_matrix *A, const gsl_matrix *K, const size_t n_cvt, gsl_matrix *S) {
  size_t n_vc=S->size1, ni_test=A->size1;
  double di, dj, tr_AK, sum_A, sum_K, s_A, s_K, sum_AK, tr_A, tr_K, d;

  for (size_t i=0; i<n_vc; i++) {
    for (size_t j=0; j<n_vc; j++) {
      tr_AK=0; sum_A=0; sum_K=0; sum_AK=0; tr_A=0; tr_K=0;
      for (size_t l=0; l<ni_test; l++) {
	s_A=0; s_K=0;
	for (size_t k=0; k<ni_test; k++) {
	  di=gsl_matrix_get(A, l, k+ni_test*i);
	  dj=gsl_matrix_get(K, l, k+ni_test*j);
	  s_A+=di; s_K+=dj;

	  tr_AK+=di*dj; sum_A+=di; sum_K+=dj;
	  if (l==k) {tr_A+=di; tr_K+=dj;}
	}
	sum_AK+=s_A*s_K;
      }

      sum_A/=(double)ni_test;
      sum_K/=(double)ni_test;
      sum_AK/=(double)ni_test;
      tr_A-=sum_A;
      tr_K-=sum_K;
      d=tr_AK-2*sum_AK+sum_A*sum_K;

      if (tr_A==0 || tr_K==0) {
	d=0;
      } else {
	d=d/(tr_A*tr_K)-1/(double)(ni_test-n_cvt);
      }

      gsl_matrix_set (S, i, j, d);
    }
  }

  //eigenlib_invert(Si);
  //cout<<tr_KiKj<<" "<<s_KiKj<<" "<<sum_Ki<<" "<<sum_Kj<<" "<<si<<" "<<sj<<" "<<d*1000000<<endl;
  return;
}



//copied from lmm.cpp; is used in the following function compKtoV
//map a number 1-(n_cvt+2) to an index between 0 and [(n_c+2)^2+(n_c+2)]/2-1
size_t GetabIndex (const size_t a, const size_t b, const size_t n_cvt) {
	if (a>n_cvt+2 || b>n_cvt+2 || a<=0 || b<=0) {cout<<"error in GetabIndex."<<endl; return 0;}
	size_t index;
	size_t l, h;
	if (b>a) {l=a; h=b;} else {l=b; h=a;}

	size_t n=n_cvt+2;
	index=(2*n-l+2)*(l-1)/2+h-l;

	return index;
}

//from an existing n by nd (centered) G matrix, compute the d+1 by d*(d-1)/2*(d+1) Q matrix
//where inside i'th d+1 by d+1 matrix, each element is tr(KiKlKjKm)-r*tr(KmKiKl)-r*tr(KlKjKm)+r^2*tr(KlKm), where r=n/(n-1)
void compKtoV (const gsl_matrix *G, gsl_matrix *V) {
  size_t n_vc=G->size2/G->size1, ni_test=G->size1;

  gsl_matrix *KiKj=gsl_matrix_alloc(ni_test, (n_vc*(n_vc+1))/2*ni_test);
  gsl_vector *trKiKj=gsl_vector_alloc( n_vc*(n_vc+1)/2 );
  gsl_vector *trKi=gsl_vector_alloc(n_vc);

  double d, tr, r=(double)ni_test/(double)(ni_test-1);
  size_t t, t_il, t_jm, t_lm, t_im, t_jl, t_ij;

  //compute KiKj for all pairs of i and j (not including the identity matrix)
  t=0;
  for (size_t i=0; i<n_vc; i++) {
    gsl_matrix_const_view Ki=gsl_matrix_const_submatrix(G, 0, i*ni_test, ni_test, ni_test);
    for (size_t j=i; j<n_vc; j++) {
      gsl_matrix_const_view Kj=gsl_matrix_const_submatrix(G, 0, j*ni_test, ni_test, ni_test);
      gsl_matrix_view KiKj_sub=gsl_matrix_submatrix (KiKj, 0, t*ni_test, ni_test, ni_test);
      eigenlib_dgemm ("N", "N", 1.0, &Ki.matrix, &Kj.matrix, 0.0, &KiKj_sub.matrix);
      t++;
    }
  }
  /*
  for (size_t i=0; i<5; i++) {
    for (size_t j=0; j<5; j++) {
      cout<<gsl_matrix_get (G, i, j)<<" ";
    }
    cout<<endl;
  }
  */

  //compute trKi, trKiKj
  t=0;
  for (size_t i=0; i<n_vc; i++) {
    for (size_t j=i; j<n_vc; j++) {
      tr=0;
      for (size_t k=0; k<ni_test; k++) {
	tr+=gsl_matrix_get (KiKj, k, t*ni_test+k);
      }
      gsl_vector_set (trKiKj, t, tr);

      t++;
    }

    tr=0;
    for (size_t k=0; k<ni_test; k++) {
      tr+=gsl_matrix_get (G, k, i*ni_test+k);
    }
    gsl_vector_set (trKi, i, tr);
  }

  //compute V
  for (size_t i=0; i<n_vc; i++) {
    for (size_t j=i; j<n_vc; j++) {
      t_ij=GetabIndex (i+1, j+1, n_vc-2);
      for (size_t l=0; l<n_vc+1; l++) {
	for (size_t m=0; m<n_vc+1; m++) {
	  if (l!=n_vc && m!=n_vc) {
	    t_il=GetabIndex (i+1, l+1, n_vc-2);
	    t_jm=GetabIndex (j+1, m+1, n_vc-2);
	    t_lm=GetabIndex (l+1, m+1, n_vc-2);
	    //cout<<ni_test<<" "<<r<<t_ij<<" "<<t_il<<" "<<t_jl<<" "<<endl;
	    tr=0;
	    for (size_t k=0; k<ni_test; k++) {
	      gsl_vector_const_view KiKl_row=gsl_matrix_const_subrow (KiKj, k, t_il*ni_test, ni_test);
	      gsl_vector_const_view KiKl_col=gsl_matrix_const_column (KiKj, t_il*ni_test+k);
	      gsl_vector_const_view KjKm_row=gsl_matrix_const_subrow (KiKj, k, t_jm*ni_test, ni_test);
	      gsl_vector_const_view KjKm_col=gsl_matrix_const_column (KiKj, t_jm*ni_test+k);

	      gsl_vector_const_view Kl_row=gsl_matrix_const_subrow (G, k, l*ni_test, ni_test);
	      gsl_vector_const_view Km_row=gsl_matrix_const_subrow (G, k, m*ni_test, ni_test);

	      if (i<=l && j<=m) {
		gsl_blas_ddot (&KiKl_row.vector, &KjKm_col.vector, &d);
		tr+=d;
		gsl_blas_ddot (&Km_row.vector, &KiKl_col.vector, &d);
		tr-=r*d;
		gsl_blas_ddot (&Kl_row.vector, &KjKm_col.vector, &d);
		tr-=r*d;
	      } else if (i<=l && j>m) {
		gsl_blas_ddot (&KiKl_row.vector, &KjKm_row.vector, &d);
		tr+=d;
		gsl_blas_ddot (&Km_row.vector, &KiKl_col.vector, &d);
		tr-=r*d;
		gsl_blas_ddot (&Kl_row.vector, &KjKm_row.vector, &d);
		tr-=r*d;
	      } else if (i>l && j<=m) {
		gsl_blas_ddot (&KiKl_col.vector, &KjKm_col.vector, &d);
		tr+=d;
		gsl_blas_ddot (&Km_row.vector, &KiKl_row.vector, &d);
		tr-=r*d;
		gsl_blas_ddot (&Kl_row.vector, &KjKm_col.vector, &d);
		tr-=r*d;
	      } else {
		gsl_blas_ddot (&KiKl_col.vector, &KjKm_row.vector, &d);
		tr+=d;
		gsl_blas_ddot (&Km_row.vector, &KiKl_row.vector, &d);
		tr-=r*d;
		gsl_blas_ddot (&Kl_row.vector, &KjKm_row.vector, &d);
		tr-=r*d;
	      }
	    }

	    tr+=r*r*gsl_vector_get (trKiKj, t_lm);
	  } else if (l!=n_vc && m==n_vc) {
	    t_il=GetabIndex (i+1, l+1, n_vc-2);
	    t_jl=GetabIndex (j+1, l+1, n_vc-2);
	    tr=0;
	    for (size_t k=0; k<ni_test; k++) {
	      gsl_vector_const_view KiKl_row=gsl_matrix_const_subrow (KiKj, k, t_il*ni_test, ni_test);
	      gsl_vector_const_view KiKl_col=gsl_matrix_const_column (KiKj, t_il*ni_test+k);
	      gsl_vector_const_view Kj_row=gsl_matrix_const_subrow (G, k, j*ni_test, ni_test);

	      if (i<=l) {
		gsl_blas_ddot (&KiKl_row.vector, &Kj_row.vector, &d);
		tr+=d;
	      } else {
		gsl_blas_ddot (&KiKl_col.vector, &Kj_row.vector, &d);
		tr+=d;
	      }
	    }
	    tr+=-r*gsl_vector_get (trKiKj, t_il)-r*gsl_vector_get (trKiKj, t_jl)+r*r*gsl_vector_get (trKi, l);
	  } else if (l==n_vc && m!=n_vc) {
	    t_jm=GetabIndex (j+1, m+1, n_vc-2);
	    t_im=GetabIndex (i+1, m+1, n_vc-2);
	    tr=0;
	    for (size_t k=0; k<ni_test; k++) {
	      gsl_vector_const_view KjKm_row=gsl_matrix_const_subrow (KiKj, k, t_jm*ni_test, ni_test);
	      gsl_vector_const_view KjKm_col=gsl_matrix_const_column (KiKj, t_jm*ni_test+k);
	      gsl_vector_const_view Ki_row=gsl_matrix_const_subrow (G, k, i*ni_test, ni_test);

	      if (j<=m) {
		gsl_blas_ddot (&KjKm_row.vector, &Ki_row.vector, &d);
		tr+=d;
	      } else {
		gsl_blas_ddot (&KjKm_col.vector, &Ki_row.vector, &d);
		tr+=d;
	      }
	    }
	    tr+=-r*gsl_vector_get (trKiKj, t_im)-r*gsl_vector_get (trKiKj, t_jm)+r*r*gsl_vector_get (trKi, m);
	  } else {
	    tr=gsl_vector_get (trKiKj, t_ij)-r*gsl_vector_get (trKi, i)-r*gsl_vector_get (trKi, j)+r*r*(double)(ni_test-1);
	  }

	  gsl_matrix_set (V, l, t_ij*(n_vc+1)+m, tr);
	}
      }
    }
  }

  gsl_matrix_scale (V, 1.0/pow((double)ni_test, 2) );

  gsl_matrix_free(KiKj);
  gsl_vector_free(trKiKj);
  gsl_vector_free(trKi);

  return;
}



//perform Jacknife sampling for variance of S
void JackknifeAKtoS (const gsl_matrix *W, const gsl_matrix *A, const gsl_matrix *K, gsl_matrix *S, gsl_matrix *Svar) {
  size_t n_vc=Svar->size1, ni_test=A->size1, n_cvt=W->size2;

  vector<vector<vector<double> > > trAK, sumAK;
  vector<vector<double> > sumA, sumK, trA, trK, sA, sK;
  vector<double> vec_tmp;
  double di, dj, d, m, v;

  //gsl_matrix *Stmp=gsl_matrix_alloc (n_vc, ni_test*n_vc);
  //gsl_matrix *Stmp_sub=gsl_matrix_alloc (n_vc, n_vc);

  //initialize and set all elements to zero
  for (size_t i=0; i<ni_test; i++) {
    vec_tmp.push_back(0);
  }

  for (size_t i=0; i<n_vc; i++) {
    sumA.push_back(vec_tmp);
    sumK.push_back(vec_tmp);
    trA.push_back(vec_tmp);
    trK.push_back(vec_tmp);
    sA.push_back(vec_tmp);
    sK.push_back(vec_tmp);
  }

  for (size_t i=0; i<n_vc; i++) {
    trAK.push_back(sumK);
    sumAK.push_back(sumK);
  }

  //run jackknife
  for (size_t i=0; i<n_vc; i++) {
    for (size_t l=0; l<ni_test; l++) {
      for (size_t k=0; k<ni_test; k++) {
	di=gsl_matrix_get(A, l, k+ni_test*i);
	dj=gsl_matrix_get(K, l, k+ni_test*i);

	for (size_t t=0; t<ni_test; t++) {
	  if (t==l || t==k) {continue;}
	  sumA[i][t]+=di;
	  sumK[i][t]+=dj;
	  if (l==k) {trA[i][t]+=di; trK[i][t]+=dj;}
	}
	sA[i][l]+=di;
	sK[i][l]+=dj;
      }
    }

    for (size_t t=0; t<ni_test; t++) {
      sumA[i][t]/=(double)(ni_test-1);
      sumK[i][t]/=(double)(ni_test-1);
    }
  }

  for (size_t i=0; i<n_vc; i++) {
    for (size_t j=0; j<n_vc; j++) {
      for (size_t l=0; l<ni_test; l++) {
	for (size_t k=0; k<ni_test; k++) {
	  di=gsl_matrix_get(A, l, k+ni_test*i);
	  dj=gsl_matrix_get(K, l, k+ni_test*j);
	  d=di*dj;

	  for (size_t t=0; t<ni_test; t++) {
	    if (t==l || t==k) {continue;}
	    trAK[i][j][t]+=d;
          }
	}

	for (size_t t=0; t<ni_test; t++) {
	  if (t==l) {continue;}
	  di=gsl_matrix_get(A, l, t+ni_test*i);
	  dj=gsl_matrix_get(K, l, t+ni_test*j);

	  sumAK[i][j][t]+=(sA[i][l]-di)*(sK[j][l]-dj);
	}
      }

      for (size_t t=0; t<ni_test; t++) {
	sumAK[i][j][t]/=(double)(ni_test-1);
      }

      m=0; v=0;
      for (size_t t=0; t<ni_test; t++) {
	d=trAK[i][j][t]-2*sumAK[i][j][t]+sumA[i][t]*sumK[j][t];
	if ( (trA[i][t]-sumA[i][t])==0 || (trK[j][t]-sumK[j][t])==0) {
	  d=0;
	} else {
	  d/=(trA[i][t]-sumA[i][t])*(trK[j][t]-sumK[j][t]);
	  d-=1/(double)(ni_test-n_cvt-1);
	}
	//gsl_matrix_set (Stmp, i, t*n_vc+j, d);
	//gsl_matrix_set (Stmp, j, t*n_vc+i, d);
	m+=d; v+=d*d;
      }
      m/=(double)ni_test;
      v/=(double)ni_test;
      v-=m*m;
      v*=(double)(ni_test-1);
      gsl_matrix_set (Svar, i, j, v);
      if (n_cvt==1) {
	d=gsl_matrix_get (S, i, j);
      	d=(double)ni_test*d-(double)(ni_test-1)*m;
	gsl_matrix_set (S, i, j, d);
      }
    }
  }

  /*
  for (size_t t=0; t<ni_test; t++) {
    gsl_matrix_view Stmp_view=gsl_matrix_submatrix(Stmp, 0, t*n_vc, n_vc, n_vc);
    gsl_matrix_memcpy (Stmp_sub, &Stmp_view.matrix);
    eigenlib_invert(Stmp_sub);
    gsl_matrix_memcpy (&Stmp_view.matrix, Stmp_sub);
  }

  for (size_t i=0; i<n_vc; i++) {
    for (size_t j=i; j<n_vc; j++) {
      m=0; v=0;
      for (size_t t=0; t<ni_test; t++) {
	d=gsl_matrix_get (Stmp, i, t*n_vc+j);
	m+=d;
	v+=d*d;
      }
      m/=(double)ni_test;
      v/=(double)ni_test;
      v-=m*m;
      v*=(double)(ni_test-1);

      gsl_matrix_set (Svar, i, j, v);
      d=gsl_matrix_get (Si, i, j);
      d=(double)ni_test*d-(double)(ni_test-1)*m;
      gsl_matrix_set (Si, i, j, d);
      if (i!=j) {gsl_matrix_set (Svar, j, i, v); gsl_matrix_set (Si, j, i, d);}
    }
  }

  gsl_matrix_free (Stmp);
  */
  return;
}



//compute the d by d S matrix with its d by d variance matrix of Svar, and the d+1 by d(d+1) matrix of Q for V(q)
void PARAM::CalcS (const map<string, double> &mapRS2wA, const map<string, double> &mapRS2wK, const gsl_matrix *W, gsl_matrix *A, gsl_matrix *K, gsl_matrix *S, gsl_matrix *Svar, gsl_vector *ns)  {
  string file_str;

  gsl_matrix_set_zero (S);
  gsl_matrix_set_zero (Svar);
  gsl_vector_set_zero (ns);

  //compute the kinship matrix G for multiple categories; these matrices are not centered, for convienence of Jacknife sampling
  if (!file_bfile.empty() ) {
    file_str=file_bfile+".bed";
    if (mapRS2wA.size()==0) {
      if (PlinkKin (file_str, d_pace, indicator_idv, indicator_snp, mapRS2wK, mapRS2cat, snpInfo, W, K, ns)==false) {error=true;}
    } else {
      if (PlinkKin (file_str, d_pace, indicator_idv, indicator_snp, mapRS2wA, mapRS2cat, snpInfo, W, A, ns)==false) {error=true;}
    }
  } else if (!file_geno.empty()) {
    file_str=file_geno;
    if (mapRS2wA.size()==0) {
      if (BimbamKin (file_str, d_pace, indicator_idv, indicator_snp, mapRS2wK, mapRS2cat, snpInfo, W, K, ns)==false) {error=true;}
    } else {
      if (BimbamKin (file_str, d_pace, indicator_idv, indicator_snp, mapRS2wA, mapRS2cat, snpInfo, W, A, ns)==false) {error=true;}
    }
  } else if (!file_mbfile.empty() ){
    if (mapRS2wA.size()==0) {
      if (MFILEKin (1, file_mbfile, d_pace, indicator_idv, mindicator_snp, mapRS2wK, mapRS2cat, msnpInfo, W, K, ns)==false) {error=true;}
    } else {
      if (MFILEKin (1, file_mbfile, d_pace, indicator_idv, mindicator_snp, mapRS2wA, mapRS2cat, msnpInfo, W, A, ns)==false) {error=true;}
    }
  } else if (!file_mgeno.empty()) {
    if (mapRS2wA.size()==0) {
      if (MFILEKin (0, file_mgeno, d_pace, indicator_idv, mindicator_snp, mapRS2wK, mapRS2cat, msnpInfo, W, K, ns)==false) {error=true;}
    } else {
      if (MFILEKin (0, file_mgeno, d_pace, indicator_idv, mindicator_snp, mapRS2wA, mapRS2cat, msnpInfo, W, A, ns)==false) {error=true;}
    }
  }

  if (mapRS2wA.size()==0) {
    gsl_matrix_memcpy (A, K);
  }

  //center and scale every kinship matrix inside G
  for (size_t i=0; i<n_vc; i++) {
    gsl_matrix_view Ksub=gsl_matrix_submatrix(K, 0, i*ni_test, ni_test, ni_test);
    CenterMatrix(&Ksub.matrix);
    ScaleMatrix(&Ksub.matrix);

    gsl_matrix_view Asub=gsl_matrix_submatrix(A, 0, i*ni_test, ni_test, ni_test);
    CenterMatrix(&Asub.matrix);
    ScaleMatrix(&Asub.matrix);
  }

  //based on G, compute S
  compAKtoS (A, K, W->size2, S);

  //compute Svar and update S with Jacknife
  JackknifeAKtoS (W, A, K, S, Svar);

  //based on G, compute a matrix Q that can be used to calculate the variance of q
  //compKtoV (G, V);

  return;
}



void PARAM::WriteVector (const gsl_vector *q, const gsl_vector *s, const size_t n_total, const string suffix)
{
	string file_str;
	file_str=path_out+"/"+file_out;
	file_str+=".";
	file_str+=suffix;
	file_str+=".txt";

	ofstream outfile (file_str.c_str(), ofstream::out);
	if (!outfile) {cout<<"error writing file: "<<file_str.c_str()<<endl; return;}

	outfile.precision(10);

	for (size_t i=0; i<q->size; ++i) {
	  outfile<<gsl_vector_get (q, i)<<endl;
	}

	for (size_t i=0; i<s->size; ++i) {
	  outfile<<gsl_vector_get (s, i)<<endl;
	}

	outfile<<n_total<<endl;

	outfile.close();
	outfile.clear();
	return;
}



void PARAM::WriteVar (const string suffix)
{
  string file_str, rs;
	file_str=path_out+"/"+file_out;
	file_str+=".";
	file_str+=suffix;
	file_str+=".txt.gz";

	ogzstream outfile (file_str.c_str(), ogzstream::out);
	if (!outfile) {cout<<"error writing file: "<<file_str.c_str()<<endl; return;}

	outfile.precision(10);

	if (mindicator_snp.size()!=0) {
	  for (size_t t=0; t<mindicator_snp.size(); t++) {
	    indicator_snp=mindicator_snp[t];
	    for (size_t i=0; i<indicator_snp.size(); i++) {
	      if (indicator_snp[i]==0) {continue;}
	      rs=snpInfo[i].rs_number;
	      outfile<<rs<<endl;
	    }
	  }
	} else {
	  for (size_t i=0; i<indicator_snp.size(); i++) {
	    if (indicator_snp[i]==0) {continue;}
	    rs=snpInfo[i].rs_number;
	    outfile<<rs<<endl;
	  }
	}

	outfile.close();
	outfile.clear();
	return;
}




void PARAM::WriteMatrix (const gsl_matrix *matrix_U, const string suffix)
{
	string file_str;
	file_str=path_out+"/"+file_out;
	file_str+=".";
	file_str+=suffix;
	file_str+=".txt";

	ofstream outfile (file_str.c_str(), ofstream::out);
	if (!outfile) {cout<<"error writing file: "<<file_str.c_str()<<endl; return;}

	outfile.precision(10);

	for (size_t i=0; i<matrix_U->size1; ++i) {
		for (size_t j=0; j<matrix_U->size2; ++j) {
			outfile<<gsl_matrix_get (matrix_U, i, j)<<"\t";  // Issue: there is a redundant tab at the end of every line.
		}
		outfile<<endl;
	}

	outfile.close();
	outfile.clear();
	return;
}


void PARAM::WriteVector (const gsl_vector *vector_D, const string suffix)
{
	string file_str;
	file_str=path_out+"/"+file_out;
	file_str+=".";
	file_str+=suffix;
	file_str+=".txt";

	ofstream outfile (file_str.c_str(), ofstream::out);
	if (!outfile) {cout<<"error writing file: "<<file_str.c_str()<<endl; return;}

	outfile.precision(10);

	for (size_t i=0; i<vector_D->size; ++i) {
		outfile<<gsl_vector_get (vector_D, i)<<endl;
	}

	outfile.close();
	outfile.clear();
	return;
}


void PARAM::CheckCvt ()
{
	if (indicator_cvt.size()==0) {return;}

	size_t ci_test=0;

	gsl_matrix *W=gsl_matrix_alloc (ni_test, n_cvt);

	for (vector<int>::size_type i=0; i<indicator_idv.size(); ++i) {
		if (indicator_idv[i]==0 || indicator_cvt[i]==0) {continue;}
		for (size_t j=0; j<n_cvt; ++j) {
			gsl_matrix_set (W, ci_test, j, (cvt)[i][j]);
		}
		ci_test++;
	}

	size_t flag_ipt=0;
	double v_min, v_max;
	set<size_t> set_remove;

	//check if any columns is an intercept
	for (size_t i=0; i<W->size2; i++) {
		gsl_vector_view w_col=gsl_matrix_column (W, i);
		gsl_vector_minmax (&w_col.vector, &v_min, &v_max);
		if (v_min==v_max) {flag_ipt=1; set_remove.insert (i);}
	}

	//add an intecept term if needed
	if (n_cvt==set_remove.size()) {
		indicator_cvt.clear();
		n_cvt=1;
	} else if (flag_ipt==0) {
		cout<<"no intecept term is found in the cvt file. a column of 1s is added."<<endl;
		for (vector<int>::size_type i=0; i<indicator_idv.size(); ++i) {
			if (indicator_idv[i]==0 || indicator_cvt[i]==0) {continue;}
			cvt[i].push_back(1.0);
		}

		n_cvt++;
	} else {}

	gsl_matrix_free(W);

	return;
}


//post-process phentoypes, covariates
void PARAM::ProcessCvtPhen ()
{
	//convert indicator_pheno to indicator_idv
	int k=1;
	indicator_idv.clear();
	for (size_t i=0; i<indicator_pheno.size(); i++) {
		k=1;
		for (size_t j=0; j<indicator_pheno[i].size(); j++) {
			if (indicator_pheno[i][j]==0) {k=0;}
		}
		indicator_idv.push_back(k);
	}

	//remove individuals with missing covariates
	if ((indicator_cvt).size()!=0) {
		for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
			indicator_idv[i]*=indicator_cvt[i];
		}
	}

	//remove individuals with missing gxe variables
	if ((indicator_gxe).size()!=0) {
		for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
			indicator_idv[i]*=indicator_gxe[i];
		}
	}

	//remove individuals with missing residual weights
	if ((indicator_weight).size()!=0) {
		for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
			indicator_idv[i]*=indicator_weight[i];
		}
	}

	//obtain ni_test
	ni_test=0;
	for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
	    if (indicator_idv[i]==0) {continue;}
		ni_test++;
	}



	//if subsample number is set, perform a random sub-sampling to determine the subsampled ids
	if (ni_subsample!=0) {
	  if (ni_test<ni_subsample) {
	    cout<<"error! number of subsamples is less than number of analyzed individuals. "<<endl;
	  } else {
	    //set up random environment
	    gsl_rng_env_setup();
	    gsl_rng *gsl_r;
	    const gsl_rng_type * gslType;
	    gslType = gsl_rng_default;
	    if (randseed<0) {
	      time_t rawtime;
	      time (&rawtime);
	      tm * ptm = gmtime (&rawtime);

	      randseed = (unsigned) (ptm->tm_hour%24*3600+ptm->tm_min*60+ptm->tm_sec);
	    }
	    gsl_r = gsl_rng_alloc(gslType);
	    gsl_rng_set(gsl_r, randseed);

	    //from ni_test, sub-sample ni_subsample
	    vector<size_t> a, b;
	    for (size_t i=0; i<ni_subsample; i++) {
              a.push_back(0);
	    }
	    for (size_t i=0; i<ni_test; i++) {
	      b.push_back(i);
	    }

	    gsl_ran_choose (gsl_r, static_cast<void*>(&a[0]), ni_subsample, static_cast<void*>(&b[0]), ni_test, sizeof (size_t) );

	    //re-set indicator_idv and ni_test
	    int j=0;
	    for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
	      if (indicator_idv[i]==0) {continue;}
	      if(find(a.begin(), a.end(), j) == a.end()) {
		indicator_idv[i]=0;
	      }
	      j++;
	    }
	    ni_test=ni_subsample;
	  }
	}

	//check ni_test
	if (ni_test==0) {
		error=true;
		cout<<"error! number of analyzed individuals equals 0. "<<endl;
		return;
	}

	//check covariates to see if they are correlated with each other, and to see if the intercept term is included
	//after getting ni_test
	//add or remove covariates
	if (indicator_cvt.size()!=0) {
		CheckCvt();
	} else {
		vector<double> cvt_row;
		cvt_row.push_back(1);

		for (vector<int>::size_type i=0; i<(indicator_idv).size(); ++i) {
			indicator_cvt.push_back(1);

			cvt.push_back(cvt_row);
		}
	}

	return;
}




void PARAM::CopyCvt (gsl_matrix *W)
{
	size_t ci_test=0;

	for (vector<int>::size_type i=0; i<indicator_idv.size(); ++i) {
		if (indicator_idv[i]==0 || indicator_cvt[i]==0) {continue;}
		for (size_t j=0; j<n_cvt; ++j) {
			gsl_matrix_set (W, ci_test, j, (cvt)[i][j]);
		}
		ci_test++;
	}

	return;
}


void PARAM::CopyGxe (gsl_vector *env)
{
	size_t ci_test=0;

	for (vector<int>::size_type i=0; i<indicator_idv.size(); ++i) {
		if (indicator_idv[i]==0 || indicator_gxe[i]==0) {continue;}
		gsl_vector_set (env, ci_test, gxe[i]);
		ci_test++;
	}

	return;
}

void PARAM::CopyWeight (gsl_vector *w)
{
	size_t ci_test=0;

	for (vector<int>::size_type i=0; i<indicator_idv.size(); ++i) {
		if (indicator_idv[i]==0 || indicator_weight[i]==0) {continue;}
		gsl_vector_set (w, ci_test, weight[i]);
		ci_test++;
	}

	return;
}


//if flag=0, then use indicator_idv to load W and Y
//else, use indicator_cvt to load them
void PARAM::CopyCvtPhen (gsl_matrix *W, gsl_vector *y, size_t flag)
{
	size_t ci_test=0;

	for (vector<int>::size_type i=0; i<indicator_idv.size(); ++i) {
		if (flag==0) {
			if (indicator_idv[i]==0) {continue;}
		} else {
			if (indicator_cvt[i]==0) {continue;}
		}

		gsl_vector_set (y, ci_test, (pheno)[i][0]);

		for (size_t j=0; j<n_cvt; ++j) {
			gsl_matrix_set (W, ci_test, j, (cvt)[i][j]);
		}
		ci_test++;
	}

	return;
}

//if flag=0, then use indicator_idv to load W and Y
//else, use indicator_cvt to load them
void PARAM::CopyCvtPhen (gsl_matrix *W, gsl_matrix *Y, size_t flag)
{
	size_t ci_test=0;

	for (vector<int>::size_type i=0; i<indicator_idv.size(); ++i) {
		if (flag==0) {
			if (indicator_idv[i]==0) {continue;}
		} else {
			if (indicator_cvt[i]==0) {continue;}
		}

        for (size_t j=0; j<n_ph; ++j) {
			gsl_matrix_set (Y, ci_test, j, (pheno)[i][j]);
		}
		for (size_t j=0; j<n_cvt; ++j) {
			gsl_matrix_set (W, ci_test, j, (cvt)[i][j]);
		}

		ci_test++;
	}

	return;
}





void PARAM::CopyRead (gsl_vector *log_N)
{
	size_t ci_test=0;

	for (vector<int>::size_type i=0; i<indicator_idv.size(); ++i) {
		if (indicator_idv[i]==0) {continue;}
		gsl_vector_set (log_N, ci_test, log(vec_read[i]) );
		ci_test++;
	}

	return;
}



void PARAM::ObtainWeight (const set<string> &setSnps_beta, map<string, double> &mapRS2wK)
{
  mapRS2wK.clear();

  vector<double> wsum, wcount;

  for (size_t i=0; i<n_vc; i++) {
    wsum.push_back(0.0);
    wcount.push_back(0.0);
  }

  string rs;
  if (msnpInfo.size()==0) {
    for (size_t i=0; i<snpInfo.size(); i++) {
      if (indicator_snp[i]==0) {continue;}

      rs=snpInfo[i].rs_number;
      if ( (setSnps_beta.size()==0 || setSnps_beta.count(rs)!=0) && (mapRS2wsnp.size()==0 || mapRS2wsnp.count(rs)!=0) && (mapRS2wcat.size()==0 || mapRS2wcat.count(rs)!=0) && (mapRS2cat.size()==0 || mapRS2cat.count(rs)!=0) ) {
	if (mapRS2wsnp.size()!=0) {
	  mapRS2wK[rs]=mapRS2wsnp[rs];
	  if (mapRS2cat.size()==0) {
	    wsum[0]+=mapRS2wsnp[rs];
	  } else {
	    wsum[mapRS2cat[rs]]+=mapRS2wsnp[rs];
	  }
	  wcount[0]++;
	} else {
	  mapRS2wK[rs]=1;
	}
      }

    }
  } else {
    for (size_t t=0; t<msnpInfo.size(); t++) {
      snpInfo=msnpInfo[t];
      indicator_snp=mindicator_snp[t];

      for (size_t i=0; i<snpInfo.size(); i++) {
	if (indicator_snp[i]==0) {continue;}

	rs=snpInfo[i].rs_number;
	if ( (setSnps_beta.size()==0 || setSnps_beta.count(rs)!=0) && (mapRS2wsnp.size()==0 || mapRS2wsnp.count(rs)!=0) && (mapRS2wcat.size()==0 || mapRS2wcat.count(rs)!=0) && (mapRS2cat.size()==0 || mapRS2cat.count(rs)!=0) ) {
	  if (mapRS2wsnp.size()!=0) {
	    mapRS2wK[rs]=mapRS2wsnp[rs];
	    if (mapRS2cat.size()==0) {
	      wsum[0]+=mapRS2wsnp[rs];
	    } else {
	      wsum[mapRS2cat[rs]]+=mapRS2wsnp[rs];
	    }
	    wcount[0]++;
	  } else {
	    mapRS2wK[rs]=1;
	  }
	}
      }
    }
  }

  if (mapRS2wsnp.size()!=0) {
    for (size_t i=0; i<n_vc; i++) {
      wsum[i]/=wcount[i];
    }

    for (map<string, double>::iterator it=mapRS2wK.begin(); it!=mapRS2wK.end(); ++it) {
      if (mapRS2cat.size()==0) {
	it->second/=wsum[0];
      } else {
	it->second/=wsum[mapRS2cat[it->first]];
      }
    }
  }
  return;
}


//pve_flag=0 then do not change pve; pve_flag==1, then change pve to 0 if pve < 0 and pve to 1 if pve > 1
void PARAM::UpdateWeight (const size_t pve_flag, const map<string, double> &mapRS2wK, const size_t ni_test, const gsl_vector *ns, map<string, double> &mapRS2wA)
{
  double d;
  vector<double> wsum, wcount;

  for (size_t i=0; i<n_vc; i++) {
    wsum.push_back(0.0);
    wcount.push_back(0.0);
  }

  for (map<string, double>::const_iterator it=mapRS2wK.begin(); it!=mapRS2wK.end(); ++it) {
    d=1;
    for (size_t i=0; i<n_vc; i++) {
      if (v_pve[i]>=1 && pve_flag==1) {
	d+=(double)ni_test/gsl_vector_get(ns, i)*mapRS2wcat[it->first][i];
      } else if (v_pve[i]<=0 && pve_flag==1) {
	d+=0;
      } else {
	d+=(double)ni_test/gsl_vector_get(ns, i)*mapRS2wcat[it->first][i]*v_pve[i];
      }
    }
    mapRS2wA[it->first]=1/(d*d);

    if (mapRS2cat.size()==0) {
      wsum[0]+=mapRS2wA[it->first];
      wcount[0]++;
    } else {
      wsum[mapRS2cat[it->first]]+=mapRS2wA[it->first];
      wcount[mapRS2cat[it->first]]++;
    }
  }

  for (size_t i=0; i<n_vc; i++) {
    wsum[i]/=wcount[i];
  }

  for (map<string, double>::iterator it=mapRS2wA.begin(); it!=mapRS2wA.end(); ++it) {
    if (mapRS2cat.size()==0) {
      it->second/=wsum[0];
    } else {
      it->second/=wsum[mapRS2cat[it->first]];
    }
  }
  return;
}

// this function updates indicator_snp, and save z-scores and other values into vectors
void PARAM::UpdateSNPnZ (const map<string, double> &mapRS2wA, const map<string, string> &mapRS2A1, const map<string, double> &mapRS2z, gsl_vector *w, gsl_vector *z, vector<size_t> &vec_cat)
{
  gsl_vector_set_zero (w);
  gsl_vector_set_zero (z);
  vec_cat.clear();

  string rs, a1;
  size_t c=0;
  if (msnpInfo.size()==0) {
    for (size_t i=0; i<snpInfo.size(); i++) {
      if (indicator_snp[i]==0) {continue;}

      rs=snpInfo[i].rs_number;
      a1=snpInfo[i].a_minor;

      if (mapRS2wA.count(rs)!=0) {
	if (a1==mapRS2A1.at(rs)) {
	  gsl_vector_set (z, c, mapRS2z.at(rs) );
	} else {
	  gsl_vector_set (z, c, -1*mapRS2z.at(rs) );
	}
	vec_cat.push_back(mapRS2cat.at(rs) );
	gsl_vector_set (w, c, mapRS2wA.at(rs) );

	c++;
      } else {
	indicator_snp[i]=0;
      }
    }
  } else {
    for (size_t t=0; t<msnpInfo.size(); t++) {
      snpInfo=msnpInfo[t];

      for (size_t i=0; i<snpInfo.size(); i++) {
	if (mindicator_snp[t][i]==0) {continue;}

	rs=snpInfo[i].rs_number;
	a1=snpInfo[i].a_minor;

	if (mapRS2wA.count(rs)!=0) {
	  if (a1==mapRS2A1.at(rs)) {
	    gsl_vector_set (z, c, mapRS2z.at(rs) );
	  } else {
	    gsl_vector_set (z, c, -1*mapRS2z.at(rs) );
	  }
	  vec_cat.push_back(mapRS2cat.at(rs) );
	  gsl_vector_set (w, c, mapRS2wA.at(rs) );

	  c++;
	} else {
	  mindicator_snp[t][i]=0;
	}
      }
    }
  }

  return;
}



// this function updates indicator_snp, and save z-scores and other values into vectors
void PARAM::UpdateSNP (const map<string, double> &mapRS2wA)
{
  string rs;
  if (msnpInfo.size()==0) {
    for (size_t i=0; i<snpInfo.size(); i++) {
      if (indicator_snp[i]==0) {continue;}

      rs=snpInfo[i].rs_number;

      if (mapRS2wA.count(rs)==0) {
	indicator_snp[i]=0;
      }
    }
  } else {
    for (size_t t=0; t<msnpInfo.size(); t++) {
      snpInfo=msnpInfo[t];

      for (size_t i=0; i<mindicator_snp[t].size(); i++) {
	if (mindicator_snp[t][i]==0) {continue;}

	rs=snpInfo[i].rs_number;

	if (mapRS2wA.count(rs)==0) {
	  mindicator_snp[t][i]=0;
	}
      }
    }
  }

  return;
}
