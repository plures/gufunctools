#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* This code is based on the _parse_signature code in ufunc_object.c in
   NumPy source code. Edited to not rely on any NumPy dependency
*/

/* Code that parses a numpy-like gufunc signature and extracts
   the logical description for it:

   - number of inputs.

   - number of outputs.

   - 'dimension variables' involved in the parse.

   - shapes of all inputs and outputs as a function of the dimension variables.
*/




/* Return the position of next non-white-space char in the string */
static int
_next_non_white_space(const char* str, int offset)
{
    int ret = offset;
    while (str[ret] == ' ' || str[ret] == '\t') {
        ret++;
    }
    return ret;
}

static int
_is_alpha_underscore(char ch)
{
    return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_';
}

static int
_is_alnum_underscore(char ch)
{
    return _is_alpha_underscore(ch) || (ch >= '0' && ch <= '9');
}

/*
 * Return the ending position of a variable name
 */
static int
_get_end_of_name(const char* str, int offset)
{
    int ret = offset;
    while (_is_alnum_underscore(str[ret])) {
        ret++;
    }
    return ret;
}

/*
 * Returns 1 if the dimension names pointed by s1 and s2 are the same,
 * otherwise returns 0.
 */
static int
_is_same_name(const char* s1, const char* s2)
{
    while (_is_alnum_underscore(*s1) && _is_alnum_underscore(*s2)) {
        if (*s1 != *s2) {
            return 0;
        }
        s1++;
        s2++;
    }
    return !_is_alnum_underscore(*s1) && !_is_alnum_underscore(*s2);
}

typedef struct _ufunc_mockup_struct {
    /* inputs */
    int nargs;
    int nin;

    /* outputs */
    int core_enabled; /* is it a gufunc? */
    int core_num_dim_ix;
    int *core_num_dims;
    int *core_dim_ixs;
    int *core_offsets;
} UFuncMockup;

/* this code is the actual code in NumPy adapted a bit. We will call it
   with a mockup ufunc object to remove the dependency.

   Calls to Numpy API will be either removed or changed to a standard equivalent
   function (PyArray_malloc -> malloc, etc..)
*/
static int
_parse_signature(UFuncMockup *ufunc, const char *signature)
{
    size_t len;
    char const **var_names;
    int nd = 0;             /* number of dimension of the current argument */
    int cur_arg = 0;        /* index into core_num_dims&core_offsets */
    int cur_core_dim = 0;   /* index into core_dim_ixs */
    int i = 0;
    char *parse_error = NULL;

    if (signature == NULL) {
        /*
        PyErr_SetString(PyExc_RuntimeError,
                        "_parse_signature with NULL signature");
        */
        return -1;
    }

    len = strlen(signature);
    /* No need to keep this for our usage.
    ufunc->core_signature = PyArray_malloc(sizeof(char) * (len+1));
    if (ufunc->core_signature) {
        strcpy(ufunc->core_signature, signature);
    }
    */
    /* Allocate sufficient memory to store pointers to all dimension names */
    var_names = malloc(sizeof(char const*) * len);
    if (var_names == NULL) {
        /* PyErr_NoMemory(); */
        return -1;
    }

    ufunc->core_enabled = 1;
    ufunc->core_num_dim_ix = 0;
    ufunc->core_num_dims = malloc(sizeof(int) * ufunc->nargs);
    ufunc->core_dim_ixs = malloc(sizeof(int) * len); /* shrink this later */
    ufunc->core_offsets = malloc(sizeof(int) * ufunc->nargs);
    if (ufunc->core_num_dims == NULL || ufunc->core_dim_ixs == NULL
        || ufunc->core_offsets == NULL) {
        /* PyErr_NoMemory(); */
        goto fail;
    }

    i = _next_non_white_space(signature, 0);
    while (signature[i] != '\0') {
        /* loop over input/output arguments */
        if (cur_arg == ufunc->nin) {
            /* expect "->" */
            if (signature[i] != '-' || signature[i+1] != '>') {
                parse_error = "expect '->'";
                goto fail;
            }
            i = _next_non_white_space(signature, i + 2);
        }

        /*
         * parse core dimensions of one argument,
         * e.g. "()", "(i)", or "(i,j)"
         */
        if (signature[i] != '(') {
            parse_error = "expect '('";
            goto fail;
        }
        i = _next_non_white_space(signature, i + 1);
        while (signature[i] != ')') {
            /* loop over core dimensions */
            int j = 0;
            if (!_is_alpha_underscore(signature[i])) {
                parse_error = "expect dimension name";
                goto fail;
            }
            while (j < ufunc->core_num_dim_ix) {
                if (_is_same_name(signature+i, var_names[j])) {
                    break;
                }
                j++;
            }
            if (j >= ufunc->core_num_dim_ix) {
                var_names[j] = signature+i;
                ufunc->core_num_dim_ix++;
            }
            ufunc->core_dim_ixs[cur_core_dim] = j;
            cur_core_dim++;
            nd++;
            i = _get_end_of_name(signature, i);
            i = _next_non_white_space(signature, i);
            if (signature[i] != ',' && signature[i] != ')') {
                parse_error = "expect ',' or ')'";
                goto fail;
            }
            if (signature[i] == ',')
            {
                i = _next_non_white_space(signature, i + 1);
                if (signature[i] == ')') {
                    parse_error = "',' must not be followed by ')'";
                    goto fail;
                }
            }
        }
        ufunc->core_num_dims[cur_arg] = nd;
        ufunc->core_offsets[cur_arg] = cur_core_dim-nd;
        cur_arg++;
        nd = 0;

        i = _next_non_white_space(signature, i + 1);
        if (cur_arg != ufunc->nin && cur_arg != ufunc->nargs) {
            /*
             * The list of input arguments (or output arguments) was
             * only read partially
             */
            if (signature[i] != ',') {
                parse_error = "expect ','";
                goto fail;
            }
            i = _next_non_white_space(signature, i + 1);
        }
    }
    if (cur_arg != ufunc->nargs) {
        parse_error = "incomplete signature: not all arguments found";
        goto fail;
    }
    ufunc->core_dim_ixs = realloc(ufunc->core_dim_ixs,
            sizeof(int)*cur_core_dim);
    /* check for trivial core-signature, e.g. "(),()->()" */
    if (cur_core_dim == 0) {
        ufunc->core_enabled = 0;
    }
    free((void*)var_names);
    return 0;

fail:
    free((void*)var_names);
    if (parse_error) {
        printf("%s at position %d in \"%s\"\n", parse_error, i, signature);
        /*
        PyErr_Format(PyExc_ValueError,
                     "%s at position %d in \"%s\"",
                     parse_error, i, signature);
        */
    }
    return -1;
}

int
legacy_numpy_parse_signature(const char *signature, int nin, int nargs)
{
    int rv;
    UFuncMockup mockup;
    mockup.nin = nin;
    mockup.nargs = nargs;

    rv = _parse_signature(&mockup, signature);

    return rv;
}

