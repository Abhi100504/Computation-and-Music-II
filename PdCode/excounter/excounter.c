#include "m_pd.h"
static t_class *excounter_class;
typedef struct _excounter {
    t_object x_obj;
    int i_count;
    t_float step;
    int i_down, i_up;
    t_outlet *f_out, *b_out;
} t_excounter;

void excounter_bang(t_excounter *x){
    t_float f = x->i_count; 
    x -> i_count += x -> step;
    outlet_float(x -> x_obj.ob_outlet, f);

}

void excounter_reset(t_excounter *x){
    t_float f = x->i_count; 
    x->i_count++;
    outlet_float(x -> x_obj.ob_outlet, f);

}

void excounter_set(t_excounter *x, t_floatarg f){
    t_float f = x->i_count; 
    x->i_count++;
    outlet_float(x -> x_obj.ob_outlet, f);

}

void excounter_bound(t_excounter *x, t_floatarg f1, t_floatarg f2){
    t_float f = x->i_count; 
    x->i_count++;
    outlet_float(x -> x_obj.ob_outlet, f);

}

void *excounter_new(t_symbol *x, int argc, t_atom *){
    t_excounter *x = (t_excounter *)pd_new(excounter_class);
    x -> i_count = f;
    outlet_new(&x -> x_obj, &s_float);
    return (void *) x;
}

void excounter_setup(void){
    excounter_class = class_new(gensym("excounter"), (t_newmethod)excounter_new, 0, sizeof(t_excounter), CLASS_DEFAULT, A_GIMME, 0);
    class_addbang(excounter_class, excounter_bang);
    class_addmethod(excounter_class, (t_method) excounter_reset, gensym("reset"), 0)
    class_addmethod(excounter_class, (t_method) excounter_set, gensym("set"), A_DEFFLOAT, 0)
    class_addmethod(excounter_class, (t_method) excounter_bound, gensym("bound"), A_DEFFLOAT, A_DEFFLOAT, 0)
}