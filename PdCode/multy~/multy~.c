/****************************************************
 *   This code is explicated in Chapter 3 of        *
 *   "Designing Audio Objects for Max/MSP and Pd"   *
 *   by Eric Lyon.                                  *   
 ****************************************************/

/* Obligatory Pd header */

#include "m_pd.h"

/* The class pointer */

static t_class *multy_class;

/* The object structure */

typedef struct _multy
{	
	t_object obj; // the Pd object
	t_float x_f;// for converting floats to signals
} t_multy;

/* Function prototypes */

void *multy_new(void);
void multy_dsp(t_multy *x, t_signal **sp, short *count);
t_int *multy_perform(t_int *w);

/* 
 The class definition function, analogous to the Max/MSP main() function. The name of this function
 is determined by the name of the class,class and is REQUIRED by Pd. 
 */

void multy_tilde_setup (void)
{
	/* Initialize the class */
	
	multy_class = class_new(gensym("multy~"), (t_newmethod)multy_new, 0, sizeof(t_multy), 0, 0);
	
	/* Specify signal input, with automatic float to signal conversion */
	
	CLASS_MAINSIGNALIN(multy_class, t_multy, x_f);
	
	/* Bind the DSP method, which is called when the DACs are turned on */
	
	class_addmethod(multy_class, (t_method)multy_dsp, gensym("dsp"),0);
	
	/* Print authorship message to the Pd window */
	
	post("multy~ from \"Designing Audio Objects\" by Eric Lyon");
	
}

/* The new instance routine */

void *multy_new(void)
{
	/* Instantiate a new multy~ object */
	
    t_multy *x = (t_multy *) pd_new(multy_class);
	
	/* Create one additional signal inlet */
	
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
	
	/* Create one signal outlet */
	
    outlet_new(&x->obj, gensym("signal"));
	
	/* Return a pointer to the new object */
    
    return x;
}

/* The perform routine */

t_int *multy_perform(t_int *w)
{
	/* Copy the object pointer (though unnecessary in this routine) */
	
	t_multy *x = (t_multy *) (w[1]);
	
	/* Copy signal vector pointers */
	
	t_float *in1 = (t_float *) (w[2]);
	t_float *in2 = (t_float *) (w[3]);
	t_float *out = (t_float *) (w[4]);
	
	/* Copy the signal vector size */
	
	int n = w[5];
	
	/* Perform the DSP loop */

	while(n--){
		*out++ = *in1++ * *in2++;
	}
	
	/* Return the next address in the DSP chain */
	
	return w + 6;
}

/* The DSP method */

void multy_dsp(t_multy *x, t_signal **sp, short *count)
{
	/* 
	 Attach the object to the DSP chain, passing the DSP routine multy_perform(), 
	 inlet and outlet pointers, and the signal vector size. 
	 */
	
	dsp_add(multy_perform, 5, x, sp[0]->s_vec, sp[1]->s_vec,sp[2]->s_vec, sp[0]->s_n);
}

