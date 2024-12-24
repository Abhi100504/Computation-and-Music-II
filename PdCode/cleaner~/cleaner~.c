/****************************************************
 *   This code is explicated in Chapter 8 of        *
 *   "Designing Audio Objects for Max/MSP and Pd"   *
 *   by Eric Lyon.                                  *   
 ****************************************************/

/* Required Pd Header Files */

#include "m_pd.h" 
#include "stdlib.h" 
#include "math.h" 
#include "string.h"

/* The object structure */

typedef struct _cleaner {
	t_object obj;
	t_float x_f;
	float *mag;
	float *phase;
	long vecsize;
} t_cleaner;

/* The class declaration */

static t_class *cleaner_class;

/* Function prototypes */

void *cleaner_new(void);
void cleaner_dsp(t_cleaner *x, t_signal **sp);
t_int *cleaner_perform(t_int *w);
void cleaner_free(t_cleaner *x);

/* The class definition function */

void cleaner_tilde_setup(void)
{
	cleaner_class = class_new(gensym("cleaner~"), (t_newmethod)cleaner_new, (t_method)cleaner_free, sizeof(t_cleaner), 0,0);
	CLASS_MAINSIGNALIN(cleaner_class, t_cleaner, x_f);
	class_addmethod(cleaner_class, (t_method)cleaner_dsp, gensym("dsp"), A_CANT, 0);
	post("cleaner~ from \"Designing Audio Objects\" by Eric Lyon");
}

/* The new instance routine */

void *cleaner_new(void)
{
	t_cleaner *x = (t_cleaner *)pd_new(cleaner_class);
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
    outlet_new(&x->obj, gensym("signal"));
	outlet_new(&x->obj, gensym("signal"));
	x->vecsize = 0;
	x->mag = NULL;
	return x;
}

/* The perform routine */

t_int *cleaner_perform(t_int *w)
{
	t_cleaner *x = (t_cleaner *) (w[1]);
	t_float *real_in = (t_float *) (w[2]);
	t_float *imag_in = (t_float *) (w[3]);
	t_float *threshmult = (t_float *) (w[4]);
	t_float *multiplier = (t_float *) (w[5]);
	t_float *real_out = (t_float *) (w[6]);
	t_float *imag_out = (t_float *) (w[7]);
	float *phase = x->phase;
	float *mag = x->mag;
	
	/* With ifft~ inside a block~ we only operate on 1/2 of the vector */
	
	int n = w[8] / 2; 
	float maxamp = 0.0;
	float mult; // store first value of *multiplier
	float threshold; // locally generated threshold
	int i;
	float a, b; // variables for Polar/Cartesian conversions
	
	/* Convert incoming complex spectrum to polar */
	
	for (i = 0; i <= n; i++) {
		a = (i == n ? real_in[1] : real_in[i]);
		b = (i == 0 || i == n ? 0. : imag_in[i]);
		mag[i] = hypot(a, b);
		phase[i] = -atan2(b, a);
	}
	
	/* 
	 Extract the maximum amplitude from the input vector. We
	 assume here that all amplitude values will be positive. 
	 */
	
	for(i = 0; i < n; i++){
		if(maxamp < mag[i]){
			maxamp = mag[i];
		}
	}
	
	/* 
	 Calculate the synthesis threshold relative to the 
	 maximum amplitude for the current FFT frame. 
	 */
	
	threshold = *threshmult * maxamp;
	mult = *multiplier;
	
	/* Rescale any amplitude values that fall below the threshold */
	
	for(i = 0; i < n; i++){
		if(mag[i] < threshold){
			mag[i] *= mult;
		}
	}
	
	/* Convert from polar to a complex spectrum */
	
	for ( i = 0; i <= n; i++ ) {
		real_out[i] = mag[i] * cos(phase[i]);
		if ( i != n ){
			imag_out[i] = -mag[i] * sin(phase[i]);
		} 
		else {
			imag_out[i] = 0.0;
		}
	}
	
	/* Return the next address on the DSP chain*/
	
	return w + 9;
}


/* The free routine */

void cleaner_free(t_cleaner *x)
{
	long bytesize = x->vecsize * sizeof(float);
	if(x->mag != NULL){
		freebytes(x->mag,bytesize);
		freebytes(x->phase,bytesize);
	}
}

/* The DSP method */

void cleaner_dsp(t_cleaner *x, t_signal **sp)
{
	/*
	 For demonstration purposes, we use C library functions 
	 for memory allocation here. In general, it is recommended 
	 to use the Pd memory functions, as shown in earlier chapters.
	 */
	
	long bytesize; // bytesize for magnitude and phase vectors
	long old_bytesize; // old bytesize for magnitude and phase vectors
	
	/* 
	 If the vector size has changed, either allocate, or resize memory.
	 On the first pass, x->vecsize will have been initialized to zero. 
	 */

	if(x->vecsize != sp[0]->s_n){
		old_bytesize = x->vecsize * sizeof(float);
		x->vecsize = sp[0]->s_n;
		bytesize = x->vecsize * sizeof(float);
		if(x->mag == NULL){
			x->phase = (float *) getbytes(bytesize);
			x->mag = (float *) getbytes(bytesize);
		}
		else {
			x->phase = (float *) resizebytes(x->phase, old_bytesize, bytesize);
			x->mag = (float *) resizebytes(x->mag, old_bytesize, bytesize);
		}

		/* Zero out the phase and magnitude vectors */

		memset(x->phase, 0, bytesize);
		memset(x->mag, 0, bytesize);
	}
	dsp_add(cleaner_perform, 8, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, 
			sp[4]->s_vec, sp[5]->s_vec, sp[0]->s_n);
}


