/****************************************************
 *   This code is a solution to the debugging       *
 *   problem posed in chapter 11 of                 *
 *   "Designing Audio Objects for Max/MSP and Pd"   *
 *   by Eric Lyon.                                  *   
 ****************************************************/

#include "m_pd.h"
static t_class *ramp_class;

typedef struct _ramp
{
	t_object obj;
	float x_f;
	long counter;
	long maximum;
} t_ramp;

void *ramp_new(void);
t_int *ramp_perform(t_int *w);
void ramp_dsp(t_ramp *x, t_signal **sp, short *count);

void ramp_tilde_setup (void)
{
	t_class *c = ramp_class = 
		class_new(gensym("ramp~"), (t_newmethod)ramp_new,0, sizeof(t_ramp),0,A_GIMME,0);
	class_addmethod(c, (t_method)ramp_dsp, gensym("dsp"), A_CANT, 0);
	CLASS_MAINSIGNALIN(c, t_ramp, x_f);
	post("ramp~ from \"Designing Audio Objects\" by Eric Lyon");
}	

void *ramp_new(void)
{
    t_ramp *x = (t_ramp *)pd_new(ramp_class);
	inlet_new(&x->obj, &x->obj.ob_pd, gensym("signal"), gensym("signal"));
    outlet_new(&x->obj, gensym("signal"));
	x->maximum = 44100;
	x->counter = 0;
    return x;
}

t_int *ramp_perform(t_int *w)
{	int i;
	t_ramp *x = (t_ramp *) (w[1]);
	float *trigger = (t_float *)(w[2]);
	float *maxcount = (t_float *)(w[3]);
	float *out = (t_float *)(w[4]);
	int n = w[5];
	long maximum = x->maximum;
	long counter = x->counter;
	float invmax = maximum ? 1.0 / (float)maximum : 0.0;

	for(i = 0; i < n; i++){
		if(trigger[i]){
			counter = 0;
			maximum = maxcount[i];
		}
		out[i] = counter * invmax;
		if(counter < maximum){
			counter++;
		}
	}
	x->maximum = maximum;
	x->counter = counter;
	return w + 6;
}

void ramp_dsp(t_ramp *x, t_signal **sp, short *count)
{
	dsp_add(ramp_perform, 5, x, sp[0]->s_vec, sp[1]->s_vec, sp[2]->s_vec, sp[0]->s_n);
}

