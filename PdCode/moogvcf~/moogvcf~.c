/************************************************
 *   This code is explicated in Chapter 12 of   *
 *   Designing Audio Objects                    *
 *   for Max/MSP and Pd by Eric Lyon.           *
 ************************************************/

/*
 The external moogvcf~ is a port of Csound code based on
 a Moog VCF Simulation by Comajuncosas/Stilson/Smith. 
 The filter design is by Tim Stilson and Julius O. Smith III.
 The original Csound DSP design is by Josep Comajuncosas.
 The C code for the Csound Ugen moogvcf was written by Hans Mikelson.
 
 The source files for this port in the Csound distribution are 
 "biquad.c" and "biquad.h".
 
 Following is the copyright notice in the original code:
 *******************************************************************
 Copyright (C) 1998, 1999, 2001 by Hans Mikelson, Matt Gerassimoff,
 Jens Groh, John ffitch, Steven Yi
 
 This file is part of Csound.
 
 The Csound Library is free software; you can redistribute it
 and/or modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 Csound is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with Csound; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 02111-1307 USA
 *******************************************************************
 
 In order to maintain compatibility with the GNU LGPL, the code in this
 file "moogvcf~.c" is also released under version 2.1 of the GNU LGPL. This
 license applies ONLY to the code and comments in "moogvcf~.c", and does 
 not extend to any other code or documentation associated with this code.
 In particular, the LGPL does NOT apply to the book "Designing Audio Objects
 for Max/MSP and Pd" by Eric Lyon which is protected under US copyright law.
 The LGPL also does not extend to any intellectual property of Cycling '74
 which is required to compile this file into a working Max/MSP external.
 
 Following is the copyright notice for this file, "moogvcf~.c":
 
 ******************************************************************* 
 "moogvcf~.c" by Eric Lyon is free software; you can redistribute it
 and/or modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.
 
 "moogvcf~.c" is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.
 
 You should have received a copy of the GNU Lesser General Public
 License along with Csound; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 02111-1307 USA 
 *******************************************************************
 
 */

#include "m_pd.h"
#include <math.h>

/* The class declaration */

static t_class *moogvcf_class;

/* The object structure */

typedef struct _moogvcf
{
	t_object x_obj;
	t_float x_f;
	double  xnm1, y1nm1, y2nm1, y3nm1, y1n, y2n, y3n, y4n; // filter coefficients
	double onedsr; // the sampling period (1/sr)
} t_moogvcf;

/* Function prototypes */

void moogvcf_tilde_setup (void);
void *moogvcf_new(void);
t_int *moogvcf_perform(t_int *w);
void moogvcf_dsp(t_moogvcf *x, t_signal **sp, short *count);
void moogvcf_assist(t_moogvcf *x, void *b, long msg, long arg, char *dst);

/* The class setup routine */

void moogvcf_tilde_setup (void)
{
	moogvcf_class = class_new(gensym("moogvcf~"), (t_newmethod)moogvcf_new, 0, sizeof(t_moogvcf), 0, 0);
	CLASS_MAINSIGNALIN(moogvcf_class, t_moogvcf, x_f);
	class_addmethod(moogvcf_class, (t_method)moogvcf_dsp, gensym("dsp"),0);
	post("moogvcf~ from \"Designing Audio Objects\" by Eric Lyon");
}

/* The new instance routine */

void *moogvcf_new(void)
{
    t_moogvcf *x = (t_moogvcf *) pd_new(moogvcf_class);	
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("signal"), gensym("signal"));
	inlet_new(&x->x_obj, &x->x_obj.ob_pd, gensym("signal"), gensym("signal"));
    outlet_new(&x->x_obj, gensym("signal"));
    return x;
}

/* The perform routine */

t_int *moogvcf_perform(t_int *w)
{
	t_moogvcf *x = (t_moogvcf *) (w[1]);
	float *input = (t_float *)(w[2]);
	float *frequency = (t_float *)(w[3]);
	float *resonance = (t_float *)(w[4]);
	float *output = (t_float *)(w[5]);
	int n = w[6];
	double fcon;
	double onedsr = x->onedsr;
    double kp=0.0, pp1d2=0.0, scale=0.0, k=0.0;
	double xn;
    double xnm1 = x->xnm1, y1nm1 = x->y1nm1, y2nm1 = x->y2nm1, y3nm1 = x->y3nm1;
    double y1n  = x->y1n, y2n = x->y2n, y3n = x->y3n, y4n = x->y4n;	
	double freqfac = 1.7817974362806 * onedsr; /* Adjust tuning */
	
	/* Comments retained from the original Csound C code */
	
	while(n--){
		fcon  = freqfac * *frequency++; /* normalised frq. 0 to Nyquist */
        kp    = 3.6*fcon-1.6*fcon*fcon-1.0;     /* Empirical tuning */
        pp1d2 = (kp+1.0)*0.5;                   /* Timesaver */
		scale = exp((1.0-pp1d2)*1.386249);      /* Scaling factor */
        k     = *resonance++ * scale;
		xn = *input++;
		xn = xn - k * y4n; /* Inverted feed back for corner peaking */
		y1n   = (xn  + xnm1 ) * pp1d2 - kp * y1n;
		y2n   = (y1n + y1nm1) * pp1d2 - kp * y2n;
		y3n   = (y2n + y2nm1) * pp1d2 - kp * y3n;
		y4n   = (y3n + y3nm1) * pp1d2 - kp * y4n;
		y4n   = y4n - y4n * y4n * y4n / 6.0;
		xnm1  = xn;       /* Update Xn-1  */
		y1nm1 = y1n;      /* Update Y1n-1 */
		y2nm1 = y2n;      /* Update Y2n-1 */
		y3nm1 = y3n;      /* Update Y3n-1 */
		*output++   =  y4n;
	}
    x->xnm1 = xnm1; x->y1nm1 = y1nm1; x->y2nm1 = y2nm1; x->y3nm1 = y3nm1;
    x->y1n  = y1n;  x->y2n  = y2n; x->y3n = y3n; x->y4n = y4n;
	return w + 7;
}

/* The DSP method */

void moogvcf_dsp(t_moogvcf *x, t_signal **sp, short *count)
{
	/* Do not add to the DSP chain if the sampling rate is zero */
	
	if(sp[0]->s_sr){
		x->onedsr = 1.0 / sp[0]->s_sr;
		dsp_add(moogvcf_perform, 6, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[0]->s_n);
	}
}

