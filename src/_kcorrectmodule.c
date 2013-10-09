#define PY_SSIZE_T_CLEAN
#include "Python.h"
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kcorrect.h>

#include "numpy/npy_3kcompat.h"
#include "numpy/arrayobject.h"

static PyObject *kcorrectError;

static IDL_LONG nz=1000;
static IDL_LONG maxiter=10000;
static float tolerance=1.e-6;
static float zmin=0., zmax=1.e-0;
static float band_shift=0.;

static float *lambda=NULL;
static float *vmatrix=NULL;
static float *rmatrix=NULL;
static float *zvals=NULL;
static IDL_LONG nk,nv,nl;
static float *filter_lambda=NULL;
static float *filter_pass=NULL;
static IDL_LONG *filter_n=NULL;
static IDL_LONG maxn,ndim,*sizes=NULL;
static float *redshift=NULL;
static float *maggies=NULL;
static float *maggies_ivar=NULL;
static float *coeffs=NULL;
static float *chi2=NULL;

float *pyvector_to_Carrayptrs(PyArrayObject *arrayin);

float *pyvector_to_Carrayptrs(PyArrayObject *arrayin)  {
/*    int n;
    n=arrayin->dimensions[0];*/
    return (float *) arrayin->data;  /* pointer to arrayin data as float */
}

static PyObject *
kcorrect_load_templates(PyObject *self, PyObject *args)
{
    char * vfile;
    char * lfile;
    if (!PyArg_ParseTuple(args, "ss", &vfile, &lfile))
        return NULL;

	/* read in templates */
	k_read_ascii_table(&vmatrix,&ndim,&sizes,vfile);
	nl=sizes[1];
	nv=sizes[0];
	FREEVEC(sizes);
	k_read_ascii_table(&lambda,&ndim,&sizes,lfile);
	if ((sizes[0]!=nl+1)  && PyErr_Occurred()) {
		PyErr_SetString( kcorrectError,"vmatrix and lambda files incompatible.\n");
		return NULL;;
	} /* end if */
	FREEVEC(sizes);

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(kcorrect_load_templates_doc,
"loads the templates"
);

/*
static PyObject *
kcorrect_load_filters(PyObject *self, PyObject *args)
{
    
    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(kcorrect_load_filters_doc,
"loads the filters"
);
*/

static PyObject *
kcorrect_fit_coeffs(PyObject *self, PyObject *args)
{
    IDL_LONG i,j,k,niter,nchunk,ncurrchunk;
    char * ffile;
    char * cfile;
    PyArrayObject *cin, *cout;
    PyArray_Descr * dsc;
    FILE *fp;
    int nd;

    if (!PyArg_ParseTuple(args, "ss", &cfile, &ffile))
        return NULL;
    fp=fopen(cfile,"r");
    dsc = PyArray_DescrFromType(NPY_FLOAT32);
    cin = (PyArrayObject *)PyArray_FromFile(fp, dsc, -1, " ");
    nd=cin->dimensions[0]/11;
//    printf("dim: %i \n %i %i ...\n", cin->nd, cin->dimensions[0], nd);

	/* load in the filters */
	k_load_filters(&filter_n,&filter_lambda,&filter_pass,&maxn,&nk,ffile);

	/* create the rmatrix; this is a big matrix which tabulates the
     projection of each basis element b onto each filter k, as a
     function of redshift; you only have to project onto the filters
     here, since every other spectrum you will project onto the
     filters will be a linear combination of the basis elements b; you
     interpolate the rmatrix to get a particular redshift */
	rmatrix=(float *) malloc(nz*nv*nk*sizeof(float));
	zvals=(float *) malloc(nz*sizeof(float));
	for(i=0;i<nz;i++)
		zvals[i]=zmin+(zmax-zmin)*((float)i+0.5)/(float)nz;
	k_projection_table(rmatrix,nk,nv,vmatrix,lambda,nl,zvals,nz,filter_n,
                           filter_lambda,filter_pass,band_shift,maxn);

    nchunk=20;
    float *mag;
    mag=pyvector_to_Carrayptrs(cin);
	redshift=(float *) malloc((nchunk+1)*sizeof(float));
	maggies=(float *) malloc((nchunk+1)*nk*sizeof(float));
	maggies_ivar=(float *) malloc((nchunk+1)*nk*sizeof(float));
	coeffs=(float *) malloc((nchunk+1)*nv*sizeof(float));
	chi2=(float *) malloc((nchunk+1)*sizeof(float));
    for (i=0;i<nd && i<nchunk;i++) {
        redshift[i] = mag[i*11];
        for(k=0;k<nk;k++) {
            //printf("i: %i m %i  mi %i ...\n", i, ((i*11)+1)+k, ((i*11)+6)+k);
            maggies[i*nk+k] = mag[((i*11)+1)+k];
            maggies_ivar[i*nk+k] = mag[((i*11)+6)+k];
        }
    }

    ncurrchunk=nd;
    /* no direct constraints on the coeffs are included in this fit */
		k_fit_nonneg(coeffs,rmatrix,nk,nv,zvals,nz,maggies,
                             maggies_ivar,redshift,ncurrchunk,tolerance,
                             maxiter,&niter,chi2,0,0);
		for(i=0;i<ncurrchunk;i++) {
			fprintf(stdout,"%e ",redshift[i]);
			for(j=0;j<nv;j++)
				fprintf(stdout,"%e ",coeffs[i*nv+j]);
			fprintf(stdout,"\n");
		} /* end for i */

    Py_INCREF(Py_None);
    return Py_None;
}

PyDoc_STRVAR(kcorrect_fit_coeffs_doc,
"prints the redshifts and coeffs "
);

static PyMethodDef kcorrect_methods[] = {
    {"fit_coeffs", kcorrect_fit_coeffs, METH_VARARGS, kcorrect_fit_coeffs_doc},
    {"load_templates", kcorrect_load_templates, METH_VARARGS, kcorrect_load_templates_doc},
    {NULL,		NULL}		/* sentinel */
};

PyDoc_STRVAR(module_doc,
"This module provides kcorrect"
);

static struct PyModuleDef kcorrectmodule = {
	PyModuleDef_HEAD_INIT,
	"kcorrect",
	module_doc,
	-1,
	kcorrect_methods,
	NULL,
	NULL,
	NULL,
	NULL
};


PyMODINIT_FUNC
PyInit__kcorrect(void)
{
	PyObject *m;
	m = PyModule_Create(&kcorrectmodule);  
	import_array();
        if (m == NULL)
            return NULL;
        kcorrectError = PyErr_NewException("kcorrect.error", NULL, NULL);
        Py_INCREF(kcorrectError);
        PyModule_AddObject(m, "error", kcorrectError);
        return m;
}