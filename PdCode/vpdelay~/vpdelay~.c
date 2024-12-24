/****************************************************
 *   This code is explicated in Chapter 9 of        *
 *   "Designing Audio Objects for Max/MSP and Pd"   *
 *   by Eric Lyon.                                  *   
 ****************************************************/

/* Required Pd header files */

#include "m_pd.h" 
#include <math.h>

/* The class declaration */

static t_class *vpdelay_class;

/* The object structure */

typedef struct _vpdelay {
	t_object obj; // the Pd object
	t_float x_f; // for internal conversion from float to signal
	float sr; // sampling rate
	float maximum_delay_time; // maximum delay time
	long delay_length; // length of the delay line in samples
	long delay_bytes; // length of delay line in bytes
	float *delay_line; // the delay line itself
	float *read_ptr; // read pointer into delay line
	float *write_ptr; // write pointer into delay line
	float delay_time; // current delay time
	float feedback; // feedback multiplier
	short delaytime_connected; // inlet connection status
	short feedback_connected; // inlet connection status
	float fdelay; // the fractional delay time
	long idelay; // the integer delay time
	float fraction; // the fractional difference between the fractional and integer delay times
	float srms; //sampling rate as milliseconds
} t_vpdelay;

/* Function prototypes */

void vpdelay_tilde_setup(void);
void *vpdelay_new(t_symbol *s, short argc, t_atom *argv);
void vpdelay_dsp(t_vpdelay *x, t_signal **sp, short *count);
t_int *vpdelay_perform(t_int *w);
void vpdelay_free(t_vpdelay *x);

/* The object setup function */

void vpdelay_tilde_setup(void)
{	
	vpdelay_class = class_new(gensym("vpdelay~"), (t_newmethod)vpdelay_new, (t_method)vpdelay_free, 
							  sizeof(t_vpdelay), 0,A_GIMME,0);
	CLASS_MAINSIGNALIN(vpdelay_class, t_vpdelay, x_f);
	class_addmethod(vpdelay_class, (t_method)vpdelay_dsp, gensym("dsp"), A_CANT, 0);
	post("vpdelay~ from \"Designing Audio Objects\" by Eric Lyon");
}

/* The new instance routine */

void *vpdelay_new(t_symbol *s, short argc, t_atom *argv)
{
	/* Initialize parameters */
	
	float delmax = 100.0, deltime = 100.0, feedback = 0.1;
	
	/* Basic object setup */
	
	t_vpdelay *x = (t_vpdelay *) pd_new(vpdelay_class);
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));	
	outlet_new(&x->obj, gensym("signal"));
	
	/* Read user parameters */
	
	if(argc >= 1){ delmax = atom_getfloatarg(0, argc, argv); }
	if(argc >= 2){ deltime = atom_getfloatarg(1, argc, argv); }
	if(argc >= 3){ feedback = atom_getfloatarg(2, argc, argv); }
	
	if(delmax <= 0){
		delmax = 250.0; 
	}
	
	x->maximum_delay_time = delmax * 0.001; 
	
	x->delay_time = deltime; 
	if(x->delay_time > delmax || x->delay_time <= 0.0){
		pd_error(x, "illegal delay time: %f, reset to 1 ms", x->delay_time);
		x->delay_time = 1.0;
	}
	
	/* force memory initialization in the DSP method by setting the sampling rate to zero */
	
	x->sr = 0.0; 
	x->feedback = feedback;
	return x;
}

/* The free memory routine */

void vpdelay_free(t_vpdelay *x)
{
	freebytes(x->delay_line, x->delay_bytes);
}

/* The perform routine */

t_int *vpdelay_perform(t_int *w)
{
	t_vpdelay *x = (t_vpdelay *) (w[1]);
	t_float *input = (t_float *) (w[2]);
	t_float *delaytime = (t_float *) (w[3]);
	t_float *feedback = (t_float *) (w[4]);
	t_float *output = (t_float *) (w[5]);
	int n = w[6];
	
	float sr = x->sr;
	float *delay_line = x->delay_line;
	float  *read_ptr  = x->read_ptr;
	float  *write_ptr = x->write_ptr;
	long delay_length = x->delay_length;
	float *endmem = delay_line + delay_length;
	short delaytime_connected = x->delaytime_connected;
	short feedback_connected = x->feedback_connected;
	float delaytime_float = x->delay_time;
	float feedback_float = x->feedback;	
	float fraction;
	float fdelay;
	float samp1, samp2;
	long idelay;
	float srms = sr / 1000.0;
	float out_sample, feedback_sample;
	
	while(n--){
		fdelay = *delaytime++ * srms;
		while(fdelay > delay_length){
			fdelay -= delay_length;
		}
		while(fdelay < 0){
			fdelay += delay_length;
		}
		idelay = (int)fdelay;
		fraction = fdelay - idelay;
		read_ptr = write_ptr - idelay;
		while(read_ptr < delay_line){
			read_ptr += delay_length;
		}
		samp1 = *read_ptr++;
		if(read_ptr == endmem){
			read_ptr = delay_line;
		}
		samp2 = *read_ptr;
		out_sample = samp1 + fraction * (samp2-samp1);
		feedback_sample = out_sample * *feedback++;	
		if(fabs(feedback_sample)  < 0.000001){
			feedback_sample = 0.0;
		}
		*write_ptr++ = *input++ + feedback_sample;
		*output++ = out_sample;
		if(write_ptr == endmem){
			write_ptr = delay_line;
		}
	}
	x->write_ptr = write_ptr;
	return w + 7;
}

void vpdelay_dsp(t_vpdelay *x, t_signal **sp, short *count)
{
	int i;
	int oldbytesize = x->delay_bytes;
	/* Exit if the sampling rate is zero */
	
	if(!sp[0]->s_sr){
		return;
	}
	
	/* Store the states of signal inlet connections */
	
	x->delaytime_connected = 1;
	x->feedback_connected = 1;
	
	/* Reset the delayline if the sampling rate has changed */
	
	if(x->sr != sp[0]->s_sr){
		x->sr = sp[0]->s_sr; 
		x->delay_length = x->sr * x->maximum_delay_time + 1;
		x->delay_bytes = x->delay_length * sizeof(float);
		
		/* 
		 If the delayline pointer is NULL, allocate a new memory block. Note the
		 use of sysmem_newptrclear() so that the memory is returned with all of
		 its values set to zero. 
		 */
		
		if(x->delay_line == NULL){
			
			/* Allocate memory */
			
			x->delay_line = (float *) getbytes(x->delay_bytes);
		}
		
		/* 
		 Otherwise, resize the existing memory block. Note the use of sysmem_resizeptrclear()
		 to set all of the returned memory locations to zero. 
		 */
		
		else {
			
			/* Or resize memory */
			
			x->delay_line = (float *) resizebytes((void *)x->delay_line, oldbytesize, x->delay_bytes);
		}
		if(x->delay_line == NULL){
			pd_error(x, "vpdelay~: cannot realloc %d bytes of memory", x->delay_bytes);
			return;
		}
		
		/* Clear the delay line */
		
		for(i = 0; i < x->delay_length; i++){
			x->delay_line[i] = 0.0;
		}
		
		/* Assign the write pointer to the start of the delayline */
		
		x->write_ptr = x->delay_line; 
	}
	dsp_add(vpdelay_perform, 6, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[3]->s_vec, sp[0]->s_n);
}
