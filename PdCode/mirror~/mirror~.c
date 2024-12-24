/****************************************************
 *   This code is used in Chapter 1 of              *
 *   "Designing Audio Objects for Max/MSP and Pd"   *
 *   by Eric Lyon.                                  *   
 ****************************************************/

/* Obligatory Pd header*/

#include "m_pd.h"

/* The class pointer */

t_class *mirror_class;

/* The object structure */

typedef struct _mirror
{	
	t_object obj; // The Pd object
	t_float x_f;
} t_mirror;

/* Function prototypes */

void *mirror_new(void);
void mirror_dsp(t_mirror *x, t_signal **sp, short *count);
t_int *mirror_perform(t_int *w);

/* The class setup function, analogous to the Max/MSP main() function */

void mirror_tilde_setup (void)
{
	/* Initialize the class */
	
	mirror_class = class_new(gensym("mirror~"), (t_newmethod)mirror_new, 0, sizeof(t_mirror), 0, 0);

	/* Specify signal input, with automatic float to signal conversion */

	CLASS_MAINSIGNALIN(mirror_class, t_mirror, x_f);

	/* Bind the DSP method, which is called when the DACs are turned on */
	
	class_addmethod(mirror_class, (t_method)mirror_dsp, gensym("dsp"),0);
	
	/* Print authorship message to the Max window */
	
	post("mirror~ from \"Designing Audio Objects\" by Eric Lyon");
}

/* The new instance routine */

void *mirror_new( void )
{
	/* Instantiate a new mirror~ object with one signal inlet */
	
    t_mirror *x = (t_mirror *) pd_new(mirror_class);
	
	/* Create one signal outlet */
	
    outlet_new(&x->obj, gensym("signal"));
 
	/* Return a pointer to the new object */

    return x;
}

/* The perform routine */

t_int *mirror_perform(t_int *w)
{
	/* Copy the signal inlet pointer */
	
	float *in = (t_float *) (w[1]);
	
	/* Copy the signal outlet pointer */
	
	float *out = (t_float *) (w[2]);
	
	/* Copy the signal vector size */
	
	int n = w[3];
	
	/* Copy 'n' samples from the signal inlet to the signal outlet */
	
	while (n--) { 
		*out++ = *in++;
	}
	/* Return the next address on the signal chain */
	
	return w + 4;
}


/* The DSP method */

void mirror_dsp(t_mirror *x, t_signal **sp, short *count)
{
	/* Call the dsp_add() function, passing the DSP routine to
	 be used, which is mirror_perform() in this case; the number of remaining 
	 arguments; a pointer to the signal inlet; a pointer to the signal outlet; 
	 and finally, the signal vector size in samples.
	 */
	dsp_add(mirror_perform, 3, sp[0]->s_vec, sp[1]->s_vec, sp[0]->s_n);
}

