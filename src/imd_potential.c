
/******************************************************************************
*
* IMD -- The ITAP Molecular Dynamics Program
*
* Copyright 1996-2007 Institute for Theoretical and Applied Physics,
* University of Stuttgart, D-70550 Stuttgart
*
******************************************************************************/

/*****************************************************************************
*
*  Routines for reading tabulated potentials and functions.
*  The tables are accessed with the macros in potaccess.h.
*  The acces functions in this file are currently not used.
*  Potentials are stored in a data structure of type pot_table_t.
*  When reading a new potential, cellsz must be set to the maximum
*  of cellsz and the maximum of the new squared potential cutoffs.
*  If necessary, cellsz must then be broadcasted again.
*
*  $Revision$
*  $Date$
*
******************************************************************************/

#include "imd.h"
#include "potaccess.h"

#ifdef  DOUBLE
#define FORMAT1 "%lf"
#define FORMAT3 "%lf %lf %lf"
#else
#define FORMAT1 "%f"
#define FORMAT3 "%f %f %f"
#endif

/*****************************************************************************
*
*  setup_potential
*
*****************************************************************************/

void setup_potentials( void )
{
#ifdef PAIR
  /* read pair potential file - also used for TTBP, EAM2, TERSOFF, EWALD, .. */
  if (have_potfile)
    read_pot_table(&pair_pot,potfilename,ntypes*ntypes,1);
  /* initialize analytically defined potentials */
  if (have_pre_pot) init_pre_pot();
#ifdef MULTIPOT
  for (i=0; i<N_POT_TAB; i++)
    copy_pot_table( pair_pot, &pair_pot_ar[i]);
#endif
#ifdef LINPOT
  make_lin_pot_table(pair_pot, &pair_pot_lin);
#endif
#endif

#ifdef TTBP
  /* read TTBP smoothing potential file */
  read_pot_table(&smooth_pot,ttbp_potfilename,ntypes*ntypes,1);
#endif

#ifdef EAM2
  /* read the tabulated embedding energy function */
  read_pot_table(&embed_pot,eam2_emb_E_filename,ntypes,0);
  /* read the tabulated electron density function */
  read_pot_table(&rho_h_tab,eam2_at_rho_filename,ntypes*ntypes,1);
#ifdef EEAM
  /* read the tabulated energy modification term */
  read_pot_table(&emod_pot,eeam_mod_E_filename,ntypes,0);
#endif
#endif
#ifdef ADP
  /* read ADP dipole distortion file */
  read_pot_table(&adp_upot,adp_upotfile,ntypes*ntypes,1);
  /* read ADP quadrupole distortion file */
  read_pot_table(&adp_wpot,adp_wpotfile,ntypes*ntypes,1);
#endif

#ifdef MEAM
  if (have_potfile)
    read_pot_table(&pair_pot,potfilename,ntypes*ntypes,1);
  /* read the tabulated embedding energy function */
  if (have_embed_potfile)
    read_pot_table(&embed_pot,meam_emb_E_filename,ntypes,0);
  /* read the tabulated electron density function */
  if (have_eldensity_file)
    read_pot_table(&el_density,meam_eldensity_filename,ntypes,1);    
  init_meam();
#endif

#ifdef KEATING
  init_keating();
#endif
#ifdef TTBP
  init_ttbp();
#endif
#ifdef STIWEB
  init_stiweb();
#endif
#ifdef TERSOFF
  init_tersoff();
#endif

#ifdef NEB
  MPI_Barrier(MPI_COMM_WORLD);
#endif
}

/*****************************************************************************
*
*  read potential table; choose format according to header
*
*****************************************************************************/

void read_pot_table( pot_table_t *pt, char *filename, int ncols, int radial )
{
  FILE *infile=NULL;
  char buffer[1024], msg[255];
  char *token, *res;
  int  have_header=0, have_format=0, end_header=0;
  int  size=ncols, tablesize, npot=0;
  int  format=DEFAULT_POTFILE_TYPE;  /* 2 for EAM2, 1 otherwise */
  int  i, k;

  /* read header */
  if (0==myid) {

    /* open file */
    infile = fopen(filename,"r");
    if (NULL == infile) error_str("Could not open file %s\n",filename);

    /* read the header */
    do {
      /* read one line */
      res=fgets(buffer,1024,infile);
      if (NULL == res) error_str("Unexpected end of file in %s",filename);
      /* see if it is a header line */
      if (buffer[0]=='#') {
        have_header = 1;
        /* stop after last header line */
        end_header = (buffer[1]=='E');
        /* see if it is the format line */
        if (buffer[1]=='F') {
          /* format complete? */
          if (2!=sscanf( (const char*)(buffer+2), "%d%d", &format, &size ))
            error_str("Corrupted format header line in file %s",filename);
          /* right number of columns? */
          if (size!=ncols) 
            error_str("Wrong number of data columns in file %s",filename);
          /* recognized format? */
          if ((format!=1) && (format!=2)) 
            error_str("Unrecognized format specified for file %s",filename);
          have_format=1;
	}
      } else if (have_header) { 
        /* header does not end properly */
        error_str("Corrupted header in file %s",filename);
      } else {
        /* we have no header, stop reading further */
	end_header=1;
      }
    } while (!end_header);

    /* did we have a format in the header */
    if ((have_header) && (!have_format))
      error_str("Format not specified in header of file %s",filename);

    /* rewind if there was no header */
    if (!have_header) rewind(infile);

    /* warn if we have no header */
    if ((0==myid) && (!have_header)) {
      sprintf(msg,"File %s has no header",filename);
      warning(msg);
    }

  } /* have read header */

  /* allocate info block of function table */
  pt->maxsteps = 0;
  pt->ncols    = ncols;
  pt->begin    = (real *) malloc(ncols*sizeof(real));
  pt->end      = (real *) malloc(ncols*sizeof(real));
  pt->step     = (real *) malloc(ncols*sizeof(real));
  pt->invstep  = (real *) malloc(ncols*sizeof(real));
  pt->len      = (int  *) malloc(ncols*sizeof(int ));
  if ((pt->begin   == NULL) || (pt->end == NULL) || (pt->step == NULL) || 
      (pt->invstep == NULL) || (pt->len == NULL))
    error_str("Cannot allocate info block for function table %s.",filename);

  /* catch the case where potential is identically zero */
  for (i=0; i<ncols; ++i) {
    pt->end[i] = 0.0;
    pt->len[i] = 0;
  }

  /* read table */
  if (0==myid) {
    if (format==1) read_pot_table1(pt, ncols, filename, infile, radial);
    if (format==2) read_pot_table2(pt, ncols, filename, infile, radial);
    fclose(infile);
  }

#ifdef MPI
  /* Broadcast table to other CPUs */
  MPI_Bcast( &(pt->maxsteps), 1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast( &(pt->ncols),    1, MPI_INT, 0, MPI_COMM_WORLD);
  MPI_Bcast( pt->begin,   ncols, REAL,    0, MPI_COMM_WORLD);
  MPI_Bcast( pt->end,     ncols, REAL,    0, MPI_COMM_WORLD);
  MPI_Bcast( pt->step,    ncols, REAL,    0, MPI_COMM_WORLD);
  MPI_Bcast( pt->invstep, ncols, REAL,    0, MPI_COMM_WORLD);
  MPI_Bcast( pt->len,     ncols, MPI_INT, 0, MPI_COMM_WORLD);
  tablesize = (pt->maxsteps + 2) * ncols;
  if (0 != myid) {
    pt->table = (real *) malloc(tablesize*sizeof(real));
    if (NULL==pt->table)
      error("Cannot allocate memory for function table");
  }
  MPI_Bcast( pt->table, tablesize, REAL, 0, MPI_COMM_WORLD);
  MPI_Bcast( &cellsz,           1, REAL, 0, MPI_COMM_WORLD);
#endif

#if   defined(FOURPOINT)
  init_fourpoint(pt, ncols);
#elif defined(SPLINE)
  pt->table2 = NULL;
  init_spline(pt, ncols, radial);
#else
  init_threepoint(pt, ncols);
#endif

  /* test interpolation of potential */
  if ((0==myid) && (debug_potential)) test_potential(*pt, filename, ncols);

}


/*****************************************************************************
*
*  read potential in first format: each line contains
*
*  r**2 V00 V01 V02 ... V10 V11 V12 ... VNN
*
*  N is the number of different atom types
*
*  Note that it is assumed that the r**2 are aequidistant.
*
******************************************************************************/

void read_pot_table1(pot_table_t *pt, int ncols, char *filename, 
                     FILE *infile, int radial)
{
  int i, k;
  int tablesize, npot=0;
  real val, numstep, delta;
  real r2, r2_start=0.0, r2_step;
  str255 msg;

  /* allocate the function table */
  pt->maxsteps = PSTEP;
  tablesize = ncols * pt->maxsteps;
  pt->table = (real *) malloc(tablesize*sizeof(real));
  if (NULL==pt->table)
    error_str("Cannot allocate memory for function table %s.",filename);

  /* input loop */
  while (!feof(infile)) {

    /* still some space left? */ 
    if (((npot%PSTEP) == 0) && (npot>0)) {
      pt->maxsteps += PSTEP;
      tablesize = ncols * pt->maxsteps;
      pt->table = (real *) realloc(pt->table, tablesize*sizeof(real));
      if (NULL==pt->table)
        error_str("Cannot extend memory for function table %s.",filename);
    }

    /*  read in potential */
    if (1 != fscanf(infile,FORMAT1,&r2)) break;
    if (npot==0) r2_start = r2;  /* catch first value */
    for (i=0; i<ncols; ++i) {
      if ((1 != fscanf(infile,FORMAT1, &val)) && (myid==0)) 
        error("Line incomplete in potential file.");
      *PTR_2D(pt->table,npot,i,pt->maxsteps,ncols) = val;
      if (val!=0.0) { /* catch last non-zero value */
        pt->end[i] = r2;
        pt->len[i] = npot+1;
      }
    }
    ++npot;
  }

  r2_step = (r2 - r2_start) / (npot-1);

  if ((0==myid) && (0==myrank)) {
    if (ncols==ntypes) {
      printf("Read tabulated function %s for %d atoms types.\n",
             filename,ncols);
    } else {
      printf("Read tabulated function %s for %d pairs of atoms types.\n",
             filename,ncols);
    }
  }

  /* fill info block, and shift potential to zero */
  for (i=0; i<ncols; ++i) {
    pt->begin[i] = r2_start;
    pt->step[i] = r2_step;
    pt->invstep[i] = 1.0 / r2_step;
    delta = *PTR_2D(pt->table,(npot-1),i,pt->maxsteps,ncols);
    /* if function of r2, shift potential and adjust cellsz */
    if (radial) {
      if (delta!=0.0) {
        if (0==myid)
          printf("Potential %1d%1d shifted by %f\n",
                 (i/ntypes),(i%ntypes),delta);
        for (k=0; k<npot; ++k) *PTR_2D(pt->table,k,i,pt->table,ncols) -= delta;
      }
    }
    if (radial) cellsz = MAX(cellsz,pt->end[i]);
  }

  /* increase table size for security */
  tablesize = ncols * (pt->maxsteps+2);
  pt->table = (real *) realloc(pt->table, tablesize*sizeof(real));
  if (NULL==pt->table)
    error_str("Cannot extend memory for function table %s.",filename);

}


/*****************************************************************************
*
*  read potential in second format: at the beginning <ncols> times 
*  a line of the form
*
*  r_begin r_end r_step,
*  
*  then the values of the potential (one per line), first those 
*  for atom pair  00, then an empty line (for gnuplot), then 01 and so on.
*  Analogously, if there is only one column per atom type.
*
*  Note that it is assumed that the r**2 are aequidistant.
*
******************************************************************************/

void read_pot_table2(pot_table_t *pt, int ncols, char *filename, 
                     FILE *infile, int radial)
{
  int i, k;
  int tablesize;
  real val, numstep, delta;

  /* read the info block of the function table */
  for(i=0; i<ncols; i++) {
    if (3!=fscanf(infile, FORMAT3, &pt->begin[i], &pt->end[i], &pt->step[i])) {
      if (0==myid) error_str("Info line in %s corrupt.", filename);
    }
    if (radial) cellsz = MAX(cellsz,pt->end[i]);
    pt->invstep[i] = 1.0 / pt->step[i];
    numstep        = 1 + (pt->end[i] - pt->begin[i]) / pt->step[i];
    pt->len[i]     = (int) (numstep+0.49);  
    pt->maxsteps   = MAX(pt->maxsteps, pt->len[i]);

    /* some security against rounding errors */
    if ((FABS(pt->len[i] - numstep) >= 0.1) && (0==myid)) {
      char msg[255];
      sprintf(msg,"numstep = %f rounded to %d in file %s.",
              numstep, pt->len[i], filename);
      warning(msg);
    }
  }

  /* allocate the function table */
  /* allow some extra values at the end for interpolation */
  tablesize = ncols * (pt->maxsteps+2);
  pt->table = (real *) malloc(tablesize * sizeof(real));
  if (NULL==pt->table)
    error_str("Cannot allocate memory for function table %s.",filename);

  /* input loop */
  for (i=0; i<ncols; i++) {
    for (k=0; k<pt->len[i]; k++) {
      if (1 != fscanf(infile,FORMAT1, &val)) {
        if (0==myid) error_str("wrong format in file %s.", filename);
      }
      *PTR_2D(pt->table,k,i,pt->maxsteps,ncols) = val;
    }
  }

  /* if function of r2, shift potential if necessary */
  if (radial) {
    for (i=0; i<ncols; i++) {
      delta = *PTR_2D(pt->table,pt->len[i]-1,i,pt->maxsteps,ncols);
      if (delta!=0.0) {
        if (0==myid)
          printf("Potential %1d%1d shifted by %f\n",
                 (i/ntypes),(i%ntypes),delta);
        for (k=0; k<pt->len[i]; k++) 
          *PTR_2D(pt->table,k,i,pt->table,ncols) -= delta;
      }
    }
  }

  if ((0==myid) && (0==myrank)) {
    if (ncols==ntypes) {
      printf("Read tabulated function %s for %d atoms types.\n",
             filename,ncols);
    } else {
      printf("Read tabulated function %s for %d pairs of atoms types.\n",
             filename,ncols);
    }
    printf("Maximal length of table is %d.\n",pt->maxsteps);
  }
}

#ifdef PAIR

/*****************************************************************************
*
*  Create or modify potential tables for predefined potentials
*
******************************************************************************/

void create_pot_table(pot_table_t *pt)
{
    int maxres = 0, tablesize;
    int ncols = ntypes * ntypes;
    int i, j, n, column;
    real r2_begin[10][10], r2_end[10][10], r2_step[10][10], r2_invstep[10][10];
    int len[10][10];
    real pot, grad, r2, val;

    /* Determine size of potential table */
    for (i=0; i<ntypes*(ntypes+1)/2; ++i)
      maxres = MAX(maxres,pot_res[i]);

    /* Allocate or extend memory for potential table */
    if (have_potfile==0) {  /* have no potential table yet */
      pt->ncols    = ncols;
      pt->maxsteps = maxres;
      tablesize    = ncols * (pt->maxsteps + 2);
      pt->begin    = (real *) malloc(ncols*sizeof(real));
      pt->end      = (real *) malloc(ncols*sizeof(real));
      pt->step     = (real *) malloc(ncols*sizeof(real));
      pt->invstep  = (real *) malloc(ncols*sizeof(real));
      pt->len      = (int  *) malloc(ncols*sizeof(int ));
      if ((pt->begin   == NULL) || (pt->end == NULL) || (pt->step == NULL) || 
	  (pt->invstep == NULL) || (pt->len == NULL)) 
        error("Cannot allocate info block for potential table.");
      pt->table = (real *) malloc(tablesize * sizeof(real));
      if (NULL==pt->table) 
        error("Cannot allocate memory for potential table.");
    } else {   /* we possibly have to extend potential table */
      if (maxres > pt->maxsteps) {
        pt->maxsteps = maxres;
        tablesize    = ncols * (pt->maxsteps + 2);
        pt->table    = (real *) realloc(pt->table, tablesize * sizeof(real));
        if (NULL==pt->table) 
          error("Cannot extend memory for potential table.");
      }
    }

    /* Prepare header info */
    column = 0;
    for (i=0; i<ntypes; i++) 
      for (j=i; j<ntypes; j++) {
        if ((r_cut_lin[column]>0) || (ew_r2_cut>0)) {
          r2_begin[i][j]   = r2_begin[j][i]   = SQR(r_begin[column]);
          r2_end[i][j]     = r2_end[j][i]     
                           = MAX( SQR(r_cut_lin[column]), ew_r2_cut);
          r2_step[i][j]    = r2_step[j][i] 
                           = (r2_end[i][j]-r2_begin[i][j])/(pot_res[column]-1);
          r2_invstep[i][j] = r2_invstep[j][i] = 1.0 / r2_step[i][j];
          len[i][j]        = len[j][i]        = pot_res[column];
	}
        else r2_end[i][j]  = r2_end[j][i] = 0.0;
        ++column;
      }

    /* Create or update info header */
    column = 0;
    for (i=0; i<ntypes; i++)
      for (j=0; j<ntypes; j++) { 
        if (r2_end[i][j]>0) {
          pt->begin  [column] = r2_begin  [i][j];
          pt->end    [column] = r2_end    [i][j];
          pt->step   [column] = r2_step   [i][j];
          pt->invstep[column] = r2_invstep[i][j];
          pt->len    [column] = len       [i][j];
        }
        else if (have_potfile==0) {
          if (myid==0)
            printf("WARNING: no pair potential for atom types %d and %d!",i,j);
          pt->begin  [column] = 0.0;
          pt->end    [column] = 0.0;
          pt->step   [column] = 0.0;
          pt->invstep[column] = 0.0;
          pt->len    [column] = 0;
	}
        ++column;
      }

    /* fill potential table */
    column = 0;
    for (i=0; i<ntypes; i++)
      for (j=0; j<ntypes; j++) {

        if (r2_end[i][j]>0) {

          for (n=0; n<len[i][j]; n++) {
            val = 0.0;
            r2 = r2_begin[i][j] + n * r2_step[i][j];
            /* Lennard-Jones */
            if (lj_epsilon[i][j]>0) {
              if (r2 < (1.0 - POT_TAIL) * r2_cut[i][j]) {
                pair_int_lj(&pot, &grad, i, j, r2);
                val += pot - lj_shift[i][j];
              }
              else if (r2 <= r2_cut[i][j]) {
                val += lj_aaa[i][j] * SQR(r2_cut[i][j] - r2);
              }
            }
            /* Morse */
            if (morse_epsilon[i][j]>0) {
              if (r2 < (1.0 - POT_TAIL) * r2_cut[i][j]) {
                pair_int_morse(&pot, &grad, i, j, r2);
                val += pot - morse_shift[i][j];
              }
              else if (r2 <= r2_cut[i][j]) {
                val += morse_aaa[i][j] * SQR(r2_cut[i][j] - r2);
              }
            }
            /* Buckingham */
            if (buck_sigma[i][j]>0) {
              if (r2 < (1.0 - POT_TAIL) * r2_cut[i][j]) {
                pair_int_buck(&pot, &grad, i, j, r2);
                val += pot - buck_shift[i][j];
              }
              else if (r2 <= r2_cut[i][j]) {
                val += buck_aaa[i][j] * SQR(r2_cut[i][j] - r2);
              }
            }
            /* harmonic potential for shell model */
            if (spring_cst[i][j]>0) {
	      val = 0.5 * spring_cst[i][j] * r2;
            }
#ifdef STIWEB
            if ((sw_a1[i][j] > 0) && (SQR(sw_a1[i][j]) > r2)) {
              pair_int_stiweb(&pot, &grad, i, j, r2);
              val += pot;
            }
#endif
#ifdef TERSOFF
            if ((ter_a[i][j] > 0) && (ter_r2_cut[i][j] > r2)) {
              pair_int_tersoff(&pot, i, j, r2);
              val += pot;
            }
#endif
#ifdef EWALD
            /* Coulomb potential for Ewald */
            if ((ew_r2_cut > 0) && (ew_nmax<0)) {
              if (r2 < ew_r2_cut) {
                pair_int_ewald(&pot, &grad, i, j, r2);
                val += pot - ew_shift[i][j];
                val -= SQRT(r2)*ew_fshift[i][j]*(SQRT(r2)-SQRT(ew_r2_cut));
              }
	    }
#endif
            *PTR_2D(pt->table, n, column, pt->maxsteps, ncols) = val;
          }
	}
        ++column;
      }

#if   defined(FOURPOINT)
  init_fourpoint(pt, ncols);
#elif defined(SPLINE)
  if (have_potfile==0) pt->table2 = NULL;
  init_spline(pt, ncols, 1);
#else
  init_threepoint(pt, ncols);
#endif

  /* test interpolation of potential */
  if ((0==myid) && (debug_potential)) test_potential(*pt, "pair_pot", ncols);

}

/******************************************************************************
*
*  init_pre_pot -- initialize parameters for analytic pair potentials
*
******************************************************************************/

void init_pre_pot(void) {

  int  i, j, k, m, n;
  real tmp = 0.0;

  n=0; m=0;
  for (i=0; i<ntypes; i++) 
    for (j=i; j<ntypes; j++) {

#ifdef STIWEB
      r_cut_lin[n] = MAX( r_cut_lin[n], stiweb_a1[n] );
#endif
#ifdef TERSOFF
      r_cut_lin[n] = MAX( r_cut_lin[n], ters_r_cut[n] );
#endif
      r_cut [i][j] = r_cut [j][i] =     r_cut_lin[n];
      r2_cut[i][j] = r2_cut[j][i] = SQR(r_cut_lin[n]);
      if (pot_res[n]==0) pot_res[n] = 1000;

      /* Lennard-Jones */
      lj_epsilon[i][j] = lj_epsilon[j][i] = lj_epsilon_lin[n]; 
      lj_sigma  [i][j] = lj_sigma  [j][i] = lj_sigma_lin  [n]; 
      if ((r_begin[n]==0) && (lj_epsilon_lin[n]>0)) {
        if (lj_sigma_lin[n]>0) 
          r_begin[n] = 0.1 * lj_sigma_lin[n];
        else
          error("lj_sigma must be > 0 if lj_epsilon > 0");
      }

      /* Morse */
      morse_epsilon[i][j] = morse_epsilon[j][i] = morse_epsilon_lin[n]; 
      morse_sigma  [i][j] = morse_sigma  [j][i] = morse_sigma_lin  [n]; 
      morse_alpha  [i][j] = morse_alpha  [j][i] = morse_alpha_lin  [n]; 
      if ((morse_epsilon_lin[n]>0) && (morse_sigma_lin[n]==0)) 
        error("morse_sigma must be > 0 if morse_epsilon > 0");

      /* Buckingham */
      buck_a    [i][j] = buck_a    [j][i] = buck_a_lin    [n];
      buck_c    [i][j] = buck_c    [j][i] = buck_c_lin    [n];
      buck_sigma[i][j] = buck_sigma[j][i] = buck_sigma_lin[n];
      if ((r_begin[n]==0) && (buck_sigma_lin[n]>0)) 
        r_begin[n] = 0.1 * buck_sigma_lin[n];

#ifdef STIWEB
      /* Stillinger-Weber */
      sw_a[i][j]  = sw_a[j][i]  = stiweb_a[n];
      sw_b[i][j]  = sw_b[j][i]  = stiweb_b[n];
      sw_p[i][j]  = sw_p[j][i]  = stiweb_p[n];
      sw_q[i][j]  = sw_q[j][i]  = stiweb_q[n];
      sw_a1[i][j] = sw_a1[j][i] = stiweb_a1[n];
      sw_de[i][j] = sw_de[j][i] = stiweb_de[n];
      sw_ga[i][j] = sw_ga[j][i] = stiweb_ga[n];
      sw_a2[i][j] = sw_a2[j][i] = stiweb_a2[n];
      for (k=0; k<ntypes; k++) {
	sw_la[k][i][j] = sw_la[k][j][i] = stiweb_la[m];
	m++;
      }
      if ((r_begin[n]==0) && (stiweb_a1[n]>0)) 
        r_begin[n] = 0.05 * stiweb_a1[n];
#endif

#ifdef TERSOFF
      /* Tersoff */
      ter_r_cut [i][j] = ter_r_cut [j][i] = ters_r_cut[n];
      ter_r2_cut[i][j] = ter_r2_cut[j][i] = SQR(ter_r_cut[i][j]);
      ter_r0    [i][j] = ter_r0    [j][i] = ters_r0[n];
      ter_a     [i][j] = ter_a     [j][i] = ters_a [n];
      ter_la    [i][j] = ter_la    [j][i] = ters_la[n];
#endif

#ifdef EWALD
      /* Coulomb for Ewald */
      if ((ew_r2_cut > 0) && (ew_nmax < 0) && (r_begin[n]==0)) 
        r_begin[n] = 0.2;
#endif

      n++;
    }

  /* harmonic potential for shell model */
  for (i=0; i<ntypes; i++) spring_cst[i][i] = 0.0;
  n=0;
  for (i=0; i<ntypes-1; i++)
    for (k=i+1; k<ntypes; k++) {
      spring_cst[i][k] = spring_cst[k][i] = spring_const[n]; 
      if (spring_const[n]>0) r_begin[i*ntypes+k] = 0.0;
      n++;
    }

  for (i=0; i<ntypes; ++i)
    for (j=0; j<ntypes; ++j)
      tmp = MAX( tmp, r2_cut[i][j] );
  
#ifdef EWALD
  if (ew_nmax < 0) tmp = MAX(tmp,ew_r2_cut);
#endif
  cellsz = MAX(cellsz,tmp);

  /* Shift of potentials */
  for (i=0; i<ntypes; i++) 
    for (j=0; j<ntypes; j++) {

      if (r2_cut[i][j] > 0.0) { 
        /* Lennard-Jones */
        if (lj_epsilon[i][j] > 0.0) {
          pair_int_lj( &lj_shift[i][j],&tmp, i, j,
                       (1.0 - POT_TAIL) * r2_cut[i][j]);
          lj_shift[i][j] +=  0.25 * tmp *  POT_TAIL * r2_cut[i][j];
          lj_aaa  [i][j]  = -0.25 * tmp / (POT_TAIL * r2_cut[i][j]);
          if (myid==0)
            printf("Lennard-Jones potential %1d %1d shifted by %f\n", 
	           i, j, -lj_shift[i][j]);
	}
        else lj_shift[i][j] = 0.0;
        /* Morse */
        if (morse_epsilon[i][j] > 0.0) {
          pair_int_morse( &morse_shift[i][j], &tmp, i, j,
                          (1.0 - POT_TAIL) * r2_cut[i][j]);
          morse_shift[i][j] +=  0.25 * tmp *  POT_TAIL * r2_cut[i][j];
          morse_aaa  [i][j]  = -0.25 * tmp / (POT_TAIL * r2_cut[i][j]);
          if (myid==0)
            printf("Morse potential %1d %1d shifted by %f\n", 
	           i, j, -morse_shift[i][j]);
	}
        else morse_shift[i][j] = 0.0;
        /* Buckingham */
        if (buck_sigma[i][j] > 0.0) {
          pair_int_buck( &buck_shift[i][j], &tmp, i, j, 
                         (1.0 - POT_TAIL) * r2_cut[i][j]);
          buck_shift[i][j] +=  0.25 * tmp *  POT_TAIL * r2_cut[i][j];
          buck_aaa  [i][j]  = -0.25 * tmp / (POT_TAIL * r2_cut[i][j]);
          if (myid==0)
            printf("Buckingham potential %1d %1d shifted by %f\n", 
	           i, j, -buck_shift[i][j]);
	}
        else buck_shift[i][j] = 0.0;
      } 
      else {
        lj_shift   [i][j] = 0.0;
        morse_shift[i][j] = 0.0;
        buck_shift [i][j] = 0.0;
      }
#ifdef EWALD
      /* Coulomb for Ewald */
      if ((ew_r2_cut > 0) && (ew_nmax < 0)) {
        if (SQR(charge[i]*charge[j]) > 0.0) {
          pair_int_ewald( &ew_shift[i][j], &ew_fshift[i][j], i, j, ew_r2_cut);
          if (myid==0)
            printf("Coulomb potential %1d %1d shifted by %f\n", 
	           i, j, -ew_shift[i][j]);
        } 
        else {
          ew_shift [i][j] = 0.0;
          ew_fshift[i][j] = 0.0;
	}
      }
#endif
    }

  /* Create or update potential table */
  create_pot_table(&pair_pot);

#ifdef VEC
  /* Lennard-Jones parameters for vector version */
  for (i=0; i<ntypes; i++)
    for (j=0; j<ntypes; j++) {
      lj_epsilon_vec[i*ntypes+j] = lj_epsilon[i][j];
      lj_sigma2_vec [i*ntypes+j] = SQR(lj_sigma[i][j]);
    }
#endif

#if defined(CBE)
  mk_pt();
#endif  /* CBE */

}

#endif

#if defined(FOURPOINT)

/******************************************************************************
*
*  init_fourpoint -- initialize for 4point interpolation
*
******************************************************************************/

void init_fourpoint( pot_table_t *pt, int nc )
{
  int  col, n;
  real *y;

  /* loop over columns */
  for (col=0; col<nc; col++) {

    y    = pt->table  + col;
    n    = pt->len[col];

    /* for security, we continue the last interpolation polynomial */
    y[ n   *nc] =  4*y[(n-1)*nc]- 6*y[(n-2)*nc]+ 4*y[(n-3)*nc]-  y[(n-4)*nc];
    y[(n+1)*nc] = 10*y[(n-1)*nc]-20*y[(n-2)*nc]+15*y[(n-3)*nc]-4*y[(n-4)*nc];

  }
}

#elif defined(SPLINE)

/******************************************************************************
*
*  init_spline -- initialize for spline interpolation
*
******************************************************************************/

void init_spline( pot_table_t *pt, int ncols, int radial )
{
  int size, col, n, i, k;
  real p, qn, un, step, *u = NULL, *y, *y2;

  /* (re)allocate data */
  size = pt->maxsteps + 2;
  pt->table2 = (real *) realloc(pt->table2, ncols * size * sizeof(real));
  u          = (real *) realloc(u,                  size * sizeof(real));
  if ((NULL==pt->table2) || (NULL==u))
    error("Cannot allocate memory for spline interpolation");

  /* loop over columns */
  for (col=0; col<ncols; col++) {

    y2   = pt->table2 + col;
    y    = pt->table  + col;
    n    = pt->len[col];
    step = pt->step[col];

    /* at the left end, we always take natural splines */
    y2[0] = u[0] = 0;

    for (i=1; i<n-1; i++) {
      p = 0.5 * y2[(i-1)*ncols] + 2.0;
      y2[i*ncols] = -0.5 / p;
      u[i] = (y[(i+1)*ncols] - 2*y[i*ncols] + y[(i-1)*ncols]) / step; 
      u[i] = (6.0 * u[i] / (2*step) - 0.5 * u[i-1]) / p;
    }

    /* first derivative zero at right end for radial functions,
       natural splines otherwise */
    if (radial) {
      qn = 0.5;
      un = (3.0 / step) * (y[(n-2)*ncols] - y[(n-1)*ncols]) / step;
    }
    else {
      qn = un = 0.0;
    }

    y2[(n-1)*ncols] = (un - qn * u[n-2]) / (qn * y2[(n-2)*ncols] + 1.0);
    for (k=n-2; k>=0; k--) 
      y2[k*ncols] = y2[k*ncols] * y2[(k+1)*ncols] + u[k];

    /* for security, we continue the last interpolation polynomial */
    y [n*ncols] = 2*y [(n-1)*ncols]-y [(n-2)*ncols]+SQR(step)*y2[(n-1)*ncols];
    y2[n*ncols] = 2*y2[(n-1)*ncols]-y2[(n-2)*ncols];

  }
}

#else

/******************************************************************************
*
*  init_threepoint -- initialize for 3point interpolation
*
******************************************************************************/

void init_threepoint( pot_table_t *pt, int ncols )
{
  int col, n;
  real *y;

  /* loop over columns */
  for (col=0; col<ncols; col++) {

    y    = pt->table  + col;
    n    = pt->len[col];

    /* for security, we continue the last interpolation polynomial */
    y[ n   *ncols] = 3*y[(n-1)*ncols] - 3*y[(n-2)*ncols] +   y[(n-3)*ncols];
    y[(n+1)*ncols] = 6*y[(n-1)*ncols] - 8*y[(n-2)*ncols] + 3*y[(n-3)*ncols];

  }
}

#endif

/******************************************************************************
*
*  test_potential -- test potential interpolation
*
******************************************************************************/

void test_potential(pot_table_t pt, char *basename, int ncols)
{
  real x, dx, pot, grad;
  int  i, col, is_short=0;
  FILE *out;
  char fname[255];

  for (col=0; col<ncols; col++) {
    dx = (pt.end[col] - pt.begin[col] + 0.5 * pt.step[col]) / debug_pot_res;
    sprintf(fname, "%s.%d", basename, col);
    out = fopen(fname,"w");
    fprintf(out, "# argument, value, derivative\n");
    for (i=0; i<=debug_pot_res; i++) {
      x = pt.begin[col] + i * dx;
      PAIR_INT( pot, grad, pt, col, ncols, x, is_short);
      fprintf(out, "%e %e %e\n", x, pot, grad);
      fflush(out);
    }
    fclose(out);
  }

}

#ifdef LINPOT

/******************************************************************************
*
*  test_potential -- test potential interpolation
*
******************************************************************************/

#define LIN_RES 10000

void make_lin_pot_table( pot_table_t pt, lin_pot_table_t *lpt )
{
  int i, j, ii;
  real dx, *t;

  lpt->ncols   = pt.ncols;
  lpt->begin   = (real  *) malloc( pt.ncols * sizeof(real ) );
  lpt->end     = (real  *) malloc( pt.ncols * sizeof(real ) );
  lpt->step    = (real  *) malloc( pt.ncols * sizeof(real ) );
  lpt->invstep = (real  *) malloc( pt.ncols * sizeof(real ) );
  lpt->len     = (int   *) malloc( pt.ncols * sizeof(int  ) );
  lpt->table   = (real **) malloc( pt.ncols * sizeof(real*) );
  if ((NULL==lpt->begin)   || (NULL==lpt->end) || (NULL==lpt->step) ||
      (NULL==lpt->invstep) || (NULL==lpt->len) || (NULL==lpt->table))
    error("Cannot allocate potential table");

  for (i=0; i<pt.ncols; i++) {

    lpt->table[i] = (real *) malloc( 2 * (LIN_RES + 2) * sizeof(real) );
    if (NULL==lpt->table[i]) error("Cannot allocate potential table");

    dx = (pt.end[i] - pt.begin[i]) / LIN_RES;

    lpt->begin[i]   = pt.begin[i];
    lpt->end[i]     = pt.end[i];
    lpt->step[i]    = dx;
    lpt->invstep[i] = 1.0 / dx;
    lpt->len[i]     = LIN_RES+1;

    t  = lpt->table[i];

    for (j=0; j<LIN_RES+1; j++)
      PAIR_INT2( t[2*j], t[2*j+1], pt, i, pt.ncols, pt.begin[i] + j*dx, ii );

    t[2*(LIN_RES+1)  ] = t[2*LIN_RES  ];
    t[2*(LIN_RES+1)+1] = t[2*LIN_RES+1];

  }
}

#endif

/*****************************************************************************
*
*  Free potential table 
*
******************************************************************************/

void free_pot_table(pot_table_t *pt)
{
  free(pt->begin);
  free(pt->end);
  free(pt->step);
  free(pt->invstep);
  free(pt->len);
  free(pt->table);
#ifdef SPLINE
  free(pt->table2);
#endif
}

#ifdef MULTIPOT

/*****************************************************************************
*
*  copy potential table 
*
******************************************************************************/

void copy_pot_table( pot_table_t pt, pot_table_t *npt )
{
  int size;

  size = (pt.maxsteps + 2) * pt.ncols;

  npt->ncols    = pt.ncols;
  npt->maxsteps = pt.maxsteps;

  npt->begin    = (real *) malloc( pt.ncols * sizeof(real) );
  npt->end      = (real *) malloc( pt.ncols * sizeof(real) );
  npt->step     = (real *) malloc( pt.ncols * sizeof(real) );
  npt->invstep  = (real *) malloc( pt.ncols * sizeof(real) );
  npt->table    = (real *) malloc( size     * sizeof(real) );
  if ((NULL==npt->begin)   || (NULL==npt->end) || (NULL==npt->step) ||
      (NULL==npt->invstep) || (NULL==npt->table))
    error("Cannot allocate potential table");

  memcpy( npt->begin,   pt.begin,   pt.ncols * sizeof(real) );
  memcpy( npt->end,     pt.end,     pt.ncols * sizeof(real) );
  memcpy( npt->step,    pt.step,    pt.ncols * sizeof(real) );
  memcpy( npt->invstep, pt.invstep, pt.ncols * sizeof(real) );
  memcpy( npt->table,   pt.table,   size     * sizeof(real) );

}

#endif

#ifdef PAIR

/*****************************************************************************
*
*  Evaluate Lennard-Jones potential
*
******************************************************************************/

void pair_int_lj(real *pot, real *grad, int p_typ, int q_typ, real r2)
{
  real sig_d_rad2, sig_d_rad6, sig_d_rad12;

  sig_d_rad2  = lj_sigma[p_typ][q_typ] * lj_sigma[p_typ][q_typ] / r2;
  sig_d_rad6  = sig_d_rad2 * sig_d_rad2 * sig_d_rad2;
  sig_d_rad12 = sig_d_rad6 * sig_d_rad6;

  *pot = lj_epsilon[p_typ][q_typ] * ( sig_d_rad12 - 2.0 * sig_d_rad6 );
  /* return (1/r)*dV/dr as derivative */
  *grad = -12.0 * lj_epsilon[p_typ][q_typ] / r2 
    * ( sig_d_rad12 - sig_d_rad6 );  
}

/*****************************************************************************
*
*  Evaluate Morse potential
*
******************************************************************************/

void pair_int_morse(real *pot, real *grad, int p_typ, int q_typ, real r2)
{
  real r, exppot, cexppot;

  r       = sqrt(r2);
  exppot  = exp( - morse_alpha[p_typ][q_typ] 
		 * ( r - morse_sigma[p_typ][q_typ] ) );
  cexppot = 1.0 - exppot;
 
  *pot  = morse_epsilon[p_typ][q_typ] * ( cexppot * cexppot - 1.0 );
  /* return (1/r)*dV/dr as derivative */
  *grad = 2.0 * morse_alpha[p_typ][q_typ] * morse_epsilon[p_typ][q_typ] / r
    * exppot * cexppot;
}

/*****************************************************************************
*
*  Evaluate Buckingham potential 
*
******************************************************************************/

void pair_int_buck(real *pot, real *grad, int p_typ, int q_typ, real r2)
{
  real rinv, rinv2, powpot, exppot, invs2;

  rinv   = buck_sigma[p_typ][q_typ] / sqrt((r2));
  rinv2  = rinv * rinv;
  powpot  = buck_c[p_typ][q_typ] * rinv2 * rinv2 * rinv2;
  exppot = buck_a[p_typ][q_typ] * exp ( - 1.0 / rinv );
  invs2   = 1.0 / ( buck_sigma[p_typ][q_typ] * buck_sigma[p_typ][q_typ] );

  *pot = exppot - powpot;
  /* return (1/r)*dV/dr as derivative */
  *grad = ( - exppot * rinv + 6 * powpot * rinv2 ) * invs2;
}

#ifdef EWALD

/*****************************************************************************
*
*  Evaluate Coulomb potential for EWALD 
*
******************************************************************************/

void pair_int_ewald(real *pot, real *grad, int p_typ, int q_typ, real r2)
{
  real  r, chg, fac;
  r     = SQRT(r2);
  chg   = charge[p_typ] * charge[q_typ] * ew_eps;
  fac   = chg * 2.0 * ew_kappa / sqrt( M_PI );
  *pot  = chg * erfc1(ew_kappa * r) / r;
  /* return (1/r)*dV/dr as derivative */
  *grad = - (*pot + fac * exp( -SQR(ew_kappa)*r2 ) ) / r2; 
}

#endif /* EWALD */

#endif /* PAIR */

#ifdef STIWEB

/*****************************************************************************
*
*  Evaluate pair potential for Stillinger-Weber potential 
*
******************************************************************************/

void pair_int_stiweb(real *pot, real *grad, int p_typ, int q_typ, real r2)
{
  real radius, phi_r, phi_a, inv_c, inv_r, f_cut;

  radius = sqrt(r2);
  phi_r  =   sw_a[p_typ][q_typ] * pow( radius, - sw_p[p_typ][q_typ] );
  phi_a  = - sw_b[p_typ][q_typ] * pow( radius, - sw_q[p_typ][q_typ] );
  inv_c  = radius - sw_a1[p_typ][q_typ];
  if (inv_c < -0.01 * sw_de[p_typ][q_typ]) {
    inv_c = 1.0 / inv_c;
    inv_r = 1.0 / radius;
    f_cut = exp( sw_de[p_typ][q_typ] * inv_c );
    *pot  = ( phi_r + phi_a ) * f_cut;
    *grad = ( - *pot * sw_de[p_typ][q_typ] * inv_c * inv_c 
	      - f_cut * inv_r * ( sw_p[p_typ][q_typ] * phi_r 
                                + sw_q[p_typ][q_typ] * phi_a ) ) * inv_r;
  } else {
    *pot  = 0.0;
    *grad = 0.0;
  }
}

#endif

#ifdef TERSOFF

/*****************************************************************************
*
*  Evaluate pair part of Tersoff potential 
*
******************************************************************************/

void pair_int_tersoff(real *pot, int p_typ, int q_typ, real r2)
{
  real r, tmp, fc;

  r   = sqrt(r2);
  tmp = M_PI / ( ter_r_cut[p_typ][q_typ] - ter_r0[p_typ][q_typ] );
  tmp = tmp * ( r - ter_r0[p_typ][q_typ] );

  if      ( r < ter_r0   [p_typ][q_typ] ) fc = 1.0; 
  else if ( r > ter_r_cut[p_typ][q_typ] ) fc = 0.0;
  else    fc = 0.5 * ( 1.0 + cos( tmp ) );  

  *pot = fc * ter_a[p_typ][q_typ] * exp( -ter_la[p_typ][q_typ] * r ); 
}

#endif

/*****************************************************************************
*
*  Evaluate potential table with quadratic interpolation. 
*  Returns the potential value and twice the derivative.
*  Note: we need (1/r)(dV/dr) = 2 * dV/dr^2 --> use with equidistant r^2 
*  col is p_typ * ntypes + q_typ
*
******************************************************************************/

void pair_int2(real *pot, real *grad, int *is_short, pot_table_t *pt, 
               int col, int inc, real r2)
{
  real r2a, istep, chi, p0, p1, p2, dv, d2v, *ptr;
  int  k;

  /* check for distances shorter than minimal distance in table */
  r2a = MIN(r2,pt->end[col]);
  r2a = r2a - pt->begin[col];
  if (r2a < 0) {
    r2a   = 0;
    *is_short = 1;
  }

  /* indices into potential table */
  istep = pt->invstep[col];
  k     = (int) (r2a * istep);
  chi   = (r2a - k * pt->step[col]) * istep;

  /* intermediate values */
  ptr = PTR_2D(pt->table, k, col, pt->maxsteps, inc);
  p0  = *ptr; ptr += inc;
  p1  = *ptr; ptr += inc;
  p2  = *ptr;
  dv  = p1 - p0;
  d2v = p2 - 2 * p1 + p0;

  /* potential and twice the derivative */
  *pot  = p0 + chi * dv + 0.5 * chi * (chi - 1) * d2v;
  *grad = 2 * istep * (dv + (chi - 0.5) * d2v);
}

/*****************************************************************************
*
*  Evaluate potential table with cubic interpolation. 
*  Returns the potential value and twice the derivative.
*  Note: we need (1/r)(dV/dr) = 2 * dV/dr^2 --> use with equidistant r^2 
*  col is p_typ * ntypes + q_typ
*
******************************************************************************/

void pair_int3(real *pot, real *grad, int *is_short, pot_table_t *pt, 
               int col, int inc, real r2)
{
  real r2a, istep, chi, p0, p1, p2, p3, *ptr;
  real fac0, fac1, fac2, fac3, dfac0, dfac1, dfac2, dfac3;
  int  k;

  /* check for distances shorter than minimal distance in table */
  r2a = MIN(r2,pt->end[col]);
  r2a = r2a - pt->begin[col];
  if (r2a < 0) {
    r2a = 0;
    *is_short = 1;
  }

  /* indices into potential table */
  istep = pt->invstep[col];
  k     = MAX( (int) (r2a * istep), 1 );
  chi   = (r2a - k * pt->step[col]) * istep;

  /* factors for the interpolation */
  fac0 = -(1.0/6.0) * chi * (chi-1.0) * (chi-2.0);
  fac1 =        0.5 * (chi*chi-1.0) * (chi-2.0);
  fac2 =       -0.5 * chi * (chi+1.0) * (chi-2.0);
  fac3 =  (1.0/6.0) * chi * (chi*chi-1.0);

  /* factors for the interpolation of the derivative */
  dfac0 = -(1.0/6.0) * ((3.0*chi-6.0)*chi+2.0);
  dfac1 =        0.5 * ((3.0*chi-4.0)*chi-1.0);
  dfac2 =       -0.5 * ((3.0*chi-2.0)*chi-2.0);
  dfac3 =    1.0/6.0 * (3.0*chi*chi-1.0);

  /* intermediate values */
  ptr = PTR_2D(pt->table, k-1, col, pt->maxsteps, inc);
  p0  = *ptr; ptr += inc;
  p1  = *ptr; ptr += inc;
  p2  = *ptr; ptr += inc;
  p3  = *ptr;

  /* potential energy */ 
  *pot = fac0 * p0 + fac1 * p1 + fac2 * p2 + fac3 * p3;

  /* twice the derivative */
  *grad = 2 * istep * (dfac0 * p0 + dfac1 * p1 + dfac2 * p2 + dfac3 * p3);
}

/*****************************************************************************
*
*  Evaluate tabulated function with quadratic interpolation. 
*
******************************************************************************/

void val_func2(real *val, int *is_short, pot_table_t *pt, 
               int col, int inc, real r2)
{
  real r2a, istep, chi, p0, p1, p2, dv, d2v, *ptr;
  int  k;

  /* check for distances shorter than minimal distance in table */
  r2a = MIN(r2,pt->end[col]);
  r2a = r2a - pt->begin[col];
  if (r2a < 0) {
    r2a = 0;
    *is_short = 1;
  }

  /* indices into potential table */
  istep = pt->invstep[col];
  k     = (int) (r2a * istep);
  chi   = (r2a - k * pt->step[col]) * istep;

  /* intermediate values */
  ptr = PTR_2D(pt->table, k, col, pt->maxsteps, inc);
  p0  = *ptr; ptr += inc;
  p1  = *ptr; ptr += inc;
  p2  = *ptr;
  dv  = p1 - p0;
  d2v = p2 - 2 * p1 + p0;

  /* the function value */
  *val = p0 + chi * dv + 0.5 * chi * (chi - 1) * d2v;
}

/*****************************************************************************
*
*  Evaluate tabulated function with cubic interpolation. 
*
******************************************************************************/

void val_func3(real *val, int *is_short, pot_table_t *pt, 
               int col, int inc, real r2)
{
  real r2a, istep, chi, p0, p1, p2, p3;
  real fac0, fac1, fac2, fac3, *ptr;
  int  k;

  /* check for distances shorter than minimal distance in table */
  r2a = MIN(r2,pt->end[col]);
  r2a = r2a - pt->begin[col];
  if (r2a < 0) {
    r2a = 0;
    *is_short = 1;
  }

  /* indices into potential table */
  istep = pt->invstep[col];
  k     = MAX( (int) (r2a * istep), 1 );
  chi   = (r2a - k * pt->step[col]) * istep;

  /* factors for the interpolation */
  fac0 = -(1.0/6.0) * chi * (chi-1.0) * (chi-2.0);
  fac1 =        0.5 * (chi*chi-1.0) * (chi-2.0);
  fac2 =       -0.5 * chi * (chi+1.0) * (chi-2.0);
  fac3 =  (1.0/6.0) * chi * (chi*chi-1.0);

  /* intermediate values */
  ptr = PTR_2D(pt->table, k-1, col, pt->maxsteps, inc);
  p0  = *ptr; ptr += inc;
  p1  = *ptr; ptr += inc;
  p2  = *ptr; ptr += inc;
  p3  = *ptr;

  /* the function value */ 
  *val = fac0 * p0 + fac1 * p1 + fac2 * p2 + fac3 * p3;
}

/*****************************************************************************
*
*  Evaluate the derivative of a function with quadratic interpolation. 
*  Returns *twice* the derivative.
*
******************************************************************************/

void deriv_func2(real *grad, int *is_short, pot_table_t *pt, 
                 int col, int inc, real r2)
{
  real r2a, istep, chi, p0, p1, p2, dv, d2v, *ptr;
  int  k;

  /* check for distances shorter than minimal distance in table */
  r2a = MIN(r2,pt->end[col]);
  r2a = r2a - pt->begin[col];
  if (r2a < 0) {
    r2a   = 0;
    *is_short = 1;
  }

  /* indices into potential table */
  istep = pt->invstep[col];
  k     = (int) (r2a * istep);
  chi   = (r2a - k * pt->step[col]) * istep;

  /* intermediate values */
  ptr = PTR_2D(pt->table, k, col, pt->maxsteps, inc);
  p0  = *ptr; ptr += inc;
  p1  = *ptr; ptr += inc;
  p2  = *ptr;
  dv  = p1 - p0;
  d2v = p2 - 2 * p1 + p0;

  /* twice the derivative */
  *grad = 2 * istep * (dv + (chi - 0.5) * d2v);
}

/*****************************************************************************
*
*  Evaluate the derivative of a function with cubic interpolation. 
*  Returns *twice* the derivative.
*
******************************************************************************/

void deriv_func3(real *grad, int *is_short, pot_table_t *pt, 
                 int col, int inc, real r2)
{
  real r2a, istep, chi, p0, p1, p2, p3, *ptr;
  real dfac0, dfac1, dfac2, dfac3;
  int  k;

  /* check for distances shorter than minimal distance in table */
  r2a = MIN(r2,pt->end[col]);
  r2a = r2a - pt->begin[col];
  if (r2a < 0) {
    r2a = 0;
    *is_short = 1;
  }

  /* indices into potential table */
  istep = pt->invstep[col];
  k     = MAX( (int) (r2a * istep), 1 );
  chi   = (r2a - k * pt->step[col]) * istep;

  /* factors for the interpolation of the 1. derivative */
  dfac0 = -(1.0/6.0) * ((3.0*chi-6.0)*chi+2.0);
  dfac1 =        0.5 * ((3.0*chi-4.0)*chi-1.0);
  dfac2 =       -0.5 * ((3.0*chi-2.0)*chi-2.0);
  dfac3 =    1.0/6.0 * (3.0*chi*chi-1.0);

  /* intermediate values */
  ptr = PTR_2D(pt->table, k-1, col, pt->maxsteps, inc);
  p0  = *ptr; ptr += inc;
  p1  = *ptr; ptr += inc;
  p2  = *ptr; ptr += inc;
  p3  = *ptr;

  /* twice the derivative */
  *grad = 2 * istep * (dfac0 * p0 + dfac1 * p1 + dfac2 * p2 + dfac3 * p3);
}
