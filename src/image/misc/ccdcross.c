/* 
 * CCDSTACK: corr-correlate images, first one in the reference image
 *
 *   11-apr-2022:    derived from ccdstack
 *
 */


#include <stdinc.h>
#include <getparam.h>
#include <vectmath.h>
#include <filestruct.h>
#include <image.h>
#include <extstring.h>
#include <ctype.h>

string defv[] = {
  "in=???\n       Input image files, first image sets the WCS",
  "out=???\n      Output cross image",
  "center=\n      X-Y Reference center (0-based pixels)",
  "box=\n         Half size of correlation box",
  "clip=\n        Only use values above this clip level",
  "bad=0\n        bad value to ignore",
  "VERSION=0.1\n  11-apr-2022 PJT",
  NULL,
};

string usage = "cross correlate images";


#ifndef HUGE
# define HUGE 1.0e35
#endif

#define MAXIMAGE 100

imageptr iptr[MAXIMAGE];	/* pointers to (input) images */
imageptr optr = NULL;           /* could do array */

real     badval;
int      nimage;                /* actual number of input images */
int      center[2];              /* center of reference point image */
int      box;
bool     Qclip;
real     clip;


local void do_cross(int l0, int l);

extern int minmax_image (imageptr iptr);

void nemo_main ()
{
    string *fnames;
    stream  instr;                      /* input files */
    stream  outstr;                     /* output file */
    int     nx, ny, nc;
    int     ll;
    
    badval = getrparam("bad");
    Qclip = hasvalue("clip");
    if (Qclip) clip = getrparam("clip");

    
    fnames = burststring(getparam("in"), ", ");  /* input file names */
    nimage = xstrlen(fnames, sizeof(string)) - 1;
    if (nimage > MAXIMAGE) error("Too many images %d > %d", nimage, MAXIMAGE);
    dprintf(0,"Using %d images\n",nimage);

    instr   = stropen(fnames[0],"r");    /* open file */
    iptr[0] = NULL;        /* make sure to init it right */
    read_image (instr, &iptr[0]);
    dprintf(0,"Image %d: pixel size %g %g   minmax %g %g\n",
		0, Dx(iptr[0]),Dy(iptr[0]),MapMin(iptr[0]),MapMax(iptr[0]));
    strclose(instr);

    if (hasvalue("center")) {
      nc = nemoinpi(getparam("center"),center,2);
      if (nc != 2) error("center= needs 2 values");
    } else {
      center[0] = Nx(iptr[0])/2;
      center[1] = Ny(iptr[0])/2;
    }

    if (hasvalue("box"))
      box    = getiparam("box");
    else
      box    = Nx(iptr[0])/2;
    nx = ny = 2*box + 1;
    dprintf(0,"Full box size %d\n",nx);
      

    optr = NULL;
    create_cube(&optr, nx, ny, 1);
    outstr = stropen (getparam("out"),"w");  /* open output file first ... */

    for (ll=1; ll<nimage; ll++) {
        instr   = stropen(fnames[ll],"r");    /* open file */
        iptr[ll] = NULL;        /* make sure to init it right */
        read_image (instr, &iptr[ll]);
	dprintf(0,"Image %d: pixel size %g %g   minmax %g %g\n",
		ll, Dx(iptr[ll]),Dy(iptr[ll]),MapMin(iptr[ll]),MapMax(iptr[ll]));
        strclose(instr);        /* close input file */
	do_cross(0,ll);
    }

    write_image(outstr,optr); 
    strclose(outstr);

}


/* 
 *  combine input maps into an output map  --
 *
 */
local void do_cross(int l0, int l)
{
    real   sum;
    int    i, j, ix, iy, nx, ny;
    int    ix1,iy1,ix2,iy2;
    int    cx,cy;
    int    badvalues;
    
    badvalues = 0;		/* count number of bad operations */

    nx = Nx(iptr[l0]);
    ny = Ny(iptr[l0]);

    dprintf(0,"%d %d  %d  %d %d\n", nx,ny,box,l0,l);
    cx = center[0];
    cy = center[1];

    for (j=-box; j<=box; j++) {
      for (i=-box; i<=box; i++) {
	sum = 0.0;
	for (iy=-box; iy<=box; iy++) {
	  iy1 = cy + iy;
	  iy2 = iy1 + j;
	  if (iy1 < 0 || iy2 < 0 || iy1 >= ny || iy2 >= ny) continue;	  
	  for (ix=-box; ix<=box; ix++) {
	    ix1 = cx + ix;;
	    ix2 = ix1 + i;
	    if (ix1 < 0 || ix2 < 0 || ix1 >= nx || ix2 >= nx) continue;
	    // @todo   handle bad values
	    if (Qclip) {
	      if (CubeValue(iptr[l0],ix1,iy1,0) < clip) continue;
	      if (CubeValue(iptr[l], ix2,iy2,0) < clip) continue;
	    }
	    sum += CubeValue(iptr[l0],ix1,iy1,0) * CubeValue(iptr[l],ix2,iy2,0);
	  }
	}
	CubeValue(optr,i+box, j+box, 0) = sum;
      }
    }

    int ix0=0, iy0=0;
    minmax_image(optr);
    dprintf(0,"New min and max in map are: %f %f\n",MapMin(optr) ,MapMax(optr) );
    for (iy=0; iy<Ny(optr); iy++)
      for (ix=0; ix<Nx(optr); ix++)
	if (CubeValue(optr,ix,iy,0) == MapMax(optr)) {
	  ix0 = ix;
	  iy0 = iy;
	  dprintf(0,"Max cross %g @ %d %d\n",MapMax(optr),ix,iy);
	}
    real sumx=0, sumy=0, sumxx=0, sumxy=0, sumyy=0;
    int dx, dy;
    sum = 0;
    for (dy=-3; dy<=3; dy++) {        // 
      iy = iy0+dy;
      for (dx=-3; dx<=3; dx++) {
	ix = ix0+dx;
	sum   = sum   + CubeValue(optr,ix,iy,0);
	sumx  = sumx  + CubeValue(optr,ix,iy,0) * dx;
	sumy  = sumy  + CubeValue(optr,ix,iy,0) * dy;
	sumxx = sumxx + CubeValue(optr,ix,iy,0) * dx*dx;
	sumxy = sumxy + CubeValue(optr,ix,iy,0) * dx*dy;
	sumyy = sumyy + CubeValue(optr,ix,iy,0) * dy*dy;
      }
    }
    dprintf(0,"X,Y mean: %g %g\n", sumx/sum, sumy/sum);
    dprintf(0,"Center at: %g %g\n",
	    ix0-box+sumx/sum, iy0-box+sumy/sum);
    
    if (badvalues)
    	warning("There were %d bad operations in dofie",badvalues);
    
}



