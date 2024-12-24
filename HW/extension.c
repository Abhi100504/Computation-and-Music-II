// class_new
// Creates a new class object in Pd
// The function handles class setup by taking the class name, methods, size
// and settings like argument types and flags to configure the class

t_class *class_new(t_symbol *s, t_newmethod newmethod, t_method freemethod,
    size_t size, int flags, t_atomtype type1, )
{
    // Prepare for if you have a flexible number of types that the class will accept
    va_list ap;
    t_atomtype vec[MAXPDARG+1], *vp = vec; // Array to store types of arguments
    int count = 0, i; // Counters
    t_class *c; // The new class being created
    
    // Determine the type of class based on flags
    int typeflag = flags & CLASS_TYPEMASK;
    if (!typeflag) typeflag = CLASS_PATCHABLE; // Default to patchable class if no typeflag
    
    *vp = type1; // Set first arg type

    // Gather all arg types from var list
    va_start(ap, type1);
    while (*vp)
    {
        if (count == MAXPDARG)
        {
            // Print error if too many args
            pd_error(0, s ? "class %s: sorry: only %d args typechecked; use A_GIMME" : "unnamed class: sorry: only %d args typechecked; use A_GIMME", s->s_name, MAXPDARG);
            break;
        }
        vp++;
        count++;
        *vp = va_arg(ap, t_atomtype); // Get next argument
    }
    va_end(ap); // Run when done processing the arguments

    // If a method for object creation is provided, add it to the Pd object maker
    if (pd_objectmaker && newmethod)
    {
        class_addmethod(pd_objectmaker, (t_method)newmethod, s, vec[0], vec[1], vec[2], vec[3], vec[4], vec[5]);

        // Allow for object loading if has an alternative name?
        if (s && class_loadsym && !zgetfn(&pd_objectmaker, class_loadsym))
        {
            const char *loadstring = class_loadsym->s_name;
            size_t l1 = strlen(s->s_name), l2 = strlen(loadstring);
            if (l2 > l1 && !strcmp(s->s_name, loadstring + (l2 - l1)))
            {
                class_addmethod(pd_objectmaker, (t_method)newmethod, class_loadsym, vec[0], vec[1], vec[2], vec[3], vec[4], vec[5]);
            }
        }
    }

    // Allocate memory for the new class and initialize its attributes, like class name, memory, and cleanup
    c = (t_class *)t_getbytes(sizeof(*c));
    c->c_name = c->c_helpname = s; 
    c->c_size = size; 
    c->c_nmethod = 0; 
    c->c_freemethod = (t_method)freemethod; 
    
    // Set up default behaviors for common methods
    c->c_bangmethod = pd_defaultbang;
    c->c_pointermethod = pd_defaultpointer;
    c->c_floatmethod = pd_defaultfloat;
    c->c_symbolmethod = pd_defaultsymbol;
    c->c_listmethod = pd_defaultlist;
    c->c_anymethod = pd_defaultanything;

    // Configure class behavior depending on its type
    c->c_wb = (typeflag == CLASS_PATCHABLE ? &text_widgetbehavior : 0); // Default widget behavior
    c->c_pwb = 0;
    c->c_firstin = ((flags & CLASS_NOINLET) == 0); // Can have an inlet?
    c->c_patchable = (typeflag == CLASS_PATCHABLE); // Can connect to others
    c->c_gobj = (typeflag >= CLASS_GOBJ); // If graphical object
    c->c_multichannel = (flags & CLASS_MULTICHANNEL) != 0; // If support for multiple channels
    c->c_nopromotesig = (flags & CLASS_NOPROMOTESIG) != 0; // No signal promotion
    c->c_nopromoteleft = (flags & CLASS_NOPROMOTELEFT) != 0; // No left signal promotion
    c->c_drawcommand = 0; // Default no custom draw command 
    c->c_floatsignalin = 0; // Default no float signal input
    c->c_externdir = class_extern_dir; // Set external directory
    c->c_savefn = (typeflag == CLASS_PATCHABLE ? text_save : class_nosavefn); // Set save function

    // Memory management for multiple Pd instances if necessary
#ifdef PDINSTANCE
    c->c_methods = (t_methodentry **)t_getbytes(pd_ninstances * sizeof(*c->c_methods));
    for (i = 0; i < pd_ninstances; i++)
        c->c_methods[i] = t_getbytes(0);
    c->c_next = class_list; // Add to the class list
    class_list = c;
#else
    c->c_methods = t_getbytes(0); // If only a single Pd instance
#endif

#if 0
    post("class: %s", c->c_name->s_name);
#endif

    return (c); // Return the created class
}


// class_addmethod
// Adds a new method to a class in Pd
// Registers a method to handle a specific type of message like bang or float
// Can take diff arg types

void class_addmethod(t_class *c, t_method fn, t_symbol *sel,
    t_atomtype arg1, )
{
    va_list ap;
    t_atomtype argtype = arg1; // First argument type
    int nargs, i; // Counters for arguments
    
    // If the class doesnt exist, return
    if(!c)
        return;
    
    // Start processing the arguments
    va_start(ap, arg1);

    // If its a signal method, its a unique type
    if (sel == &s_signal)
    {
        if (c->c_floatsignalin)
            post("warning: signal method overrides class_mainsignalin");
        c->c_floatsignalin = -1; // Unique flag for signal method
    }

    // If its a common pd method, consider the following
    if (sel == &s_bang)
    {
        // If its not valid, go to phooey which prints an error
        if (argtype) goto phooey; 
        class_addbang(c, fn);
    }
    else if (sel == &s_float)
    {
        if (argtype != A_FLOAT || va_arg(ap, t_atomtype)) goto phooey;
        class_doaddfloat(c, fn);
    }
    else if (sel == &s_symbol)
    {
        if (argtype != A_SYMBOL || va_arg(ap, t_atomtype)) goto phooey;
        class_addsymbol(c, fn);
    }
    else if (sel == &s_list)
    {
        // Ensure the argument type can handle input
        if (argtype != A_GIMME) goto phooey;
        class_addlist(c, fn);
    }
    else if (sel == &s_anything)
    {
        if (argtype != A_GIMME) goto phooey;
        class_addanything(c, fn);
    }
    else
    {
        // For standard methods, process the list of argument types by storing them in argvec
        unsigned char argvec[MAXPDARG+1];
        nargs = 0;

        while (argtype != A_NULL && nargs < MAXPDARG)
        {
            argvec[nargs++] = argtype;
            argtype = va_arg(ap, t_atomtype);
        }

        // If too many arguments, print an error
        if (argtype != A_NULL)
            pd_error(0, "%s_%s: only 5 arguments are typecheckable; use A_GIMME",
                (c->c_name)?(c->c_name->s_name):"<anon>", sel?(sel->s_name):"<nomethod>");

        // Add a null terminator to the arg vec
        argvec[nargs] = 0;

// For each instance of Pd add the method
#ifdef PDINSTANCE
        for (i = 0; i < pd_ninstances; i++)
        {
            class_addmethodtolist(c, &c->c_methods[i], c->c_nmethod,
                (t_gotfn)fn, sel?dogensym(sel->s_name, 0, pd_instances[i]):0,
                    argvec, pd_instances[i]);
        }
#else
        // If theres only one instance, add the method to the class
        class_addmethodtolist(c, &c->c_methods, c->c_nmethod,
            (t_gotfn)fn, sel, argvec, &pd_maininstance);
#endif
        // Increment the method count
        c->c_nmethod++;
    }

    // handle any errors or cleanup
    goto done;

phooey:
    // Print an error if the argument types are wrong
    bug("class_addmethod: %s_%s: bad argument types\n",
        (c->c_name)?(c->c_name->s_name):"<anon>", sel?(sel->s_name):"<nomethod>");

done:
    // Finish processing the variable argument list
    va_end(ap);
    return;
}


// post
// A logging function that formats a message and prints to console
// Takes a format string and variable arguments and processes them into a single string

void post(const char *fmt, )
{

    char buf[MAXPDSTRING]; // Holds the final message
    va_list ap; // Variable list to hold additional arguments
    t_int arg[8]; // Array for holding up to 8 integer? arguments
    int i;
    va_start(ap, fmt); // Start processing the variable arguments
    
    // Format the input string and its arguments and store the result in the buffer
    pd_vsnprintf(buf, MAXPDSTRING-1, fmt, ap);
    va_end(ap);
    strcat(buf, "\n"); // add a newline afterwards
    dopost(buf); // log the message
}

// pd_new
// This function creates a new instance of a class
// Allocates memory for the object and initializes it based on classs properties
t_pd *pd_new(t_class *c)
{
    t_pd *x; // Pointer to the new object

    // If class not valid, send bug and return null
    if (!c) {
        bug ("pd_new: apparently called before setup routine"); 
        return NULL; 
    }

    // Allocate memory for the new object based on the classs size
    x = (t_pd *)t_getbytes(c->c_size);

    // Set the objects class to the one provided
    *x = c;

    // If the class can be connected to the Pd patches, initialize its inlets and outlets
    if (c->c_patchable)
    {
        ((t_object *)x)->ob_inlet = 0;  // Set no inlets default
        ((t_object *)x)->ob_outlet = 0; // Set no outlets default
    }

    // Return new object
    return (x);
}

// inlet_new
// Creates and initializes a new inlet for a Pd object to receive messages

t_inlet *inlet_new(t_object *owner, t_pd *dest, t_symbol *s1, t_symbol *s2)
{
    // Make new inlet and say who owns it and where messages go
    t_inlet *x = (t_inlet *)pd_new(inlet_class), *y, *y2;
    x->i_owner = owner;
    x->i_dest = dest;

    // If message type is signal set up for signal, otherwise use other symbol
    if (s1 == &s_signal)
        x->i_uniu_floatsignalvalue = 0; // For signal, set value to zero
    else 
        x->i_symto = s2; // For other types, use this symbol

    x->i_symfrom = s1;

    // If object has other inlets, find last one and add new inlet there
    if ((y = owner->ob_inlet))
    {
        while ((y2 = y->i_next)) 
            y = y2;
        y->i_next = x;
    }
    else 
        owner->ob_inlet = x;  // If no inlets exist, become the first inlet

    // Return new inlet
    return (x);
}

// outlet_new
// This function creates a new outlet for an object
// An outlet is a point where the object sends data to other objects in a patch

t_outlet *outlet_new(t_object *owner, t_symbol *s)
{
    // Allocate memory for a new outlet.
    t_outlet *x = (t_outlet *)getbytes(sizeof(*x)), *y, *y2;

    // Set the owner of the outlet
    x->o_owner = owner;

    // Initialize the next outlet pointer to null
    x->o_next = 0;

    // Check if the owner already has an outlet. If it does find the last outlet and link to the end. If not set as first
    if ((y = owner->ob_outlet))
    {
        while ((y2 = y->o_next)) y = y2;
        y->o_next = x;
    }
    else
    {
        owner->ob_outlet = x;
    }

    // Set up a connection to trace this outlet if theres backtracking
    if (backtracer_cantrace)
    {
        // Create a new backtracer and associate it with the owner
        t_backtracer *b = backtracer_new(&owner->ob_pd);

        // Allocate memory for the outlets connection data
        x->o_connections = (t_outconnect *)t_getbytes(sizeof(t_outconnect));

        // Set up the connection to point to the backtracer
        x->o_connections->oc_next = 0;
        x->o_connections->oc_to = &b->b_pd;
    }
    else
    {
        // If backtracing is not enabled, set the connection pointer to null
        x->o_connections = 0;
    }
    x->o_sym = s;

    // return new outlet
    return (x);
}

// dsp_add
// Adds a new DSP routine to the DSP chain

void dsp_add(t_perfroutine f, int n, ...)
{
    int newsize = THIS->u_dspchainsize + n + 1, i; // calculate new size of the DSP chain
    va_list ap; // setup to handle variable arguments

    // resize the DSP chain to fit the new function and arguments
    THIS->u_dspchain = t_resizebytes(THIS->u_dspchain,
        THIS->u_dspchainsize * sizeof(t_int), newsize * sizeof(t_int));

    // add the DSP function f to the end of the current DSP chain
    THIS->u_dspchain[THIS->u_dspchainsize - 1] = (t_int)f;

    // if loud is enabled, log the added function
    if (THIS->u_loud)
        post("add to chain: %lx", THIS->u_dspchain[THIS->u_dspchainsize - 1]);

    va_start(ap, n); // start processing the additional arguments

    // loop through the additional arguments and add them to the DSP chain
    for (i = 0; i < n; i++)
    {
        THIS->u_dspchain[THIS->u_dspchainsize + i] = va_arg(ap, t_int); // add each argument
        if (THIS->u_loud)
            post("add to chain: %lx", THIS->u_dspchain[THIS->u_dspchainsize + i]);
    }

    va_end(ap); // finish processing the variable arguments

    // mark the end of the DSP chain with done
    THIS->u_dspchain[newsize - 1] = (t_int)dsp_done;

    // update the DSP chain size to reflect the newly added elements
    THIS->u_dspchainsize = newsize;
}
