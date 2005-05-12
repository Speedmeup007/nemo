/* TABFILTER:   convolve a filter with a spectrum to get a flux
 *
 *
 *      10-may-05    first version, cloned off tabspline
 *
 * instr     suggested mnemonics        rough wavelengths (Angstrom)
 * -----     -------------------        ---------------------------------
 * GALEX:    NUV,FUV                    1000
 * SDSS:     u,g,r,i,z                  3000-6000
 *           U,B,V,R,I
 * 2MASS:    J,H,K                      10000-20000
 * SPITZER:  I1,I2,I3,I4,M1,M2,M3       [3.6,4.5,5.8,8.0,24,70,1xx]*10000
 *
 *
 * TODO:
 *   - flux calibration
 *   - tabular spectra
 */

#include <stdinc.h> 
#include <getparam.h>
#include <spline.h>
#include <extstring.h>

string defv[] = {
  "filter=???\n     Filter mnemonic (U,B,V,R,I,H,J,K) or full spectrum name",
  "spectrum=\n      Input spectrum table file (1=wavelength(A) 2=flux)",
  "tbb=\n           Black Body temperature, in case used",
  "xscale=1\n       Scale factor applied to input spectrum wavelength",
  "yscale=1\n       Scale factor applied to input spectrum flux",
  "step=1\n         Initial integration step (in Angstrom)",
  "VERSION=0.2\n    11-may-05 PJT",
  NULL,

};

string usage="flux derived from convolving a filter with a spectrum";

string cvsid="$Id$";


#define MAXZERO     64
#define MAXDATA  16384

extern string *burststring(string, string);
extern void freestrings(string *);
/*
 * Planck curve,  output in ergs/cm2/s/A
 *
 * @index Planck curve, input: angstrom, kelvin   output: ergs/cm2/s/A
 */

real planck(real wavelen, real T_b)
{
#if 0
  real w = wavelen/1e8;      /* w in cm now , for cgs units used here */
  real C1 = 3.74185e-5;      /* 3.74e28 */
  real C2 = 1.43883;         /* 1.44e8 */
  real x = C2/w/T_b;
  return C1/(w*sqr(sqr(w))*(exp(x)-1))*1e8;
#else
  /* w in Angstrom, output in watts/m^2 */
  real w = wavelen;
  real C1 = 3.74e28;
  real C2 = 1.44e8;
  real x = C2/w/T_b;
  return C1/(w*sqr(sqr(w))*(exp(x)-1));
#endif
}


string filtername(string shortname)
{
  int nsp;
  static char fullname[256];
  char line[MAX_LINELEN];
  string *sp, fpath;
  stream fstr;

  if (nemo_file_size(shortname) > 0) return shortname;

  fpath = getenv("NEMODAT");
  if (fpath == 0) error("NEMODAT does not exist");

  sprintf(fullname,"%s/filter/Fnames",fpath);
  dprintf(1,"Trying %s\n",fullname);
  fstr = stropen(fullname,"r");
  while (fgets(line,MAX_LINELEN,fstr)) {
    if (line[0] == '#') continue;
    sp = burststring(line," \n");
    nsp = xstrlen(sp,sizeof(sp))-1;
    if (nsp > 1) {
      if (streq(shortname,sp[0])) {
	sprintf(fullname,"%s/filter/%s",fpath,sp[1]);
	dprintf(1,"Match! Assuming %s\n",fullname);
	freestrings(sp);
	return fullname;
      }
    }
    freestrings(sp);
  }
  return shortname;
}


nemo_main()
{
  int colnr[2];
  real *coldat[2], *xdat, *ydat, xmin, xmax, ymin, ymax;
  real fy[MAXZERO], fx[MAXZERO], xp[MAXDATA], yp[MAXDATA], x, y, xd, yd, dx;
  real tbb,sum;
  stream instr;
  string fmt, stype;
  char fmt1[100], fmt2[100];
  int i, j, n, nx, ny, nmax, izero;
  bool Qx, Qy, Qxall, Qyall;
  real *sdat;
  string filter = filtername(getparam("filter"));
  
  nmax = nemo_file_lines(filter,MAXLINES);
  xdat = coldat[0] = (real *) allocate(nmax*sizeof(real));
  ydat = coldat[1] = (real *) allocate(nmax*sizeof(real));
  colnr[0] = 1;  /* wavelenght in angstrom */
  colnr[1] = 2;  /* normalized filter response [0,1] */
  
  instr = stropen(filter,"r");
  n = get_atable(instr,2,colnr,coldat,nmax);
  
  for(i=0; i<n; i++) {
    dprintf(2,fmt,xdat[i],ydat[i]);
    if (i==0) {
      xmin = xmax = xdat[0];
      ymin = ymax = ydat[0];
    } else {
      xmax = MAX(xmax,xdat[i]);
      ymax = MAX(ymax,ydat[i]);
      xmin = MIN(xmin,xdat[i]);
      ymin = MIN(ymin,ydat[i]);
    }
  }
  dprintf(1,"Filter wavelength range: %g : %g\n",xmin,xmax);
  dprintf(1,"Filter response range: %g : %g\n",ymin,ymax);
  if (ydat[0]   != 0) warning("lower edge filter response not 0: %g",ydat[0]);
  if (ydat[n-1] != 0) warning("upper edge filter response not 0: %g",ydat[n-1]);
  dx = getdparam("step");
  if ((xmax-xmin)/100 < dx) {
    warning("Integration step %g in Angstrom too large, resetting to %g",
	    dx, (xmax-xmin)/100);
    dx = (xmax-xmin)/100;
  }
  
  /* setup a spline interpolation table into the filter */
  sdat = (real *) allocate(sizeof(real)*n*3);
  spline(sdat,xdat,ydat,n);
  
  if (hasvalue("tbb")) {                /* using a Planck curve */
    tbb = getdparam("tbb");
    
    sum = 0;
    for (x = xmin; x <= xmax; x += dx) {
      y = seval(x,xdat,ydat,sdat,n);    /* filter */
      dprintf(3,"%g %g\n",x,y);
      y *= planck(x,tbb);
      sum += y;
    }
    sum *= dx;
    printf("%g %g %g\n",tbb,sum,-2.5*log10(sum));
  } else if (hasvalue("spectrum")) {
    warning("tabular spectrum not implemented yet");
  } else
    warning("Either spectrum= or tbb= must be used");
}

