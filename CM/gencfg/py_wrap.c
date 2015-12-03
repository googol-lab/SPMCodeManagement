#include <Python.h>
#include <stdio.h>
#include "datatype.h"

// wrap function for calling python functions
PyObject* python_wrap(char *module_name, char *func_name)
{
	PyObject *pName, *pModule, *pFunc;
	PyObject *pValue;

	Py_Initialize();
	pName = PyString_FromString(module_name);
	/* Error checking of pName left out */

	pModule = PyImport_Import(pName);
	Py_DECREF(pName);

	if (pModule != NULL) {
		pFunc = PyObject_GetAttrString(pModule, func_name);
		Py_DECREF(pModule);
		/* pFunc is a new reference */

		if (pFunc && PyCallable_Check(pFunc)) {
			pValue = PyObject_CallObject(pFunc, NULL);
			Py_XDECREF(pFunc);
			if (pValue == NULL) {
				PyErr_Print();
				fprintf(stderr,"Call failed\n");
				return NULL;
			}


		}
		else {
			Py_DECREF(pFunc);
			if (PyErr_Occurred())
				PyErr_Print();
			fprintf(stderr, "Cannot find function \"%s\"\n", func_name);
			return NULL;
		}
	}
	else {
		PyErr_Print();
		fprintf(stderr, "Failed to load \"%s\"\n", module_name);
		return NULL;
	}
	Py_Finalize();
	return pValue;
}

addr_t get_min_addr(void)
{
    PyObject* pValue = python_wrap("py_func", "get_min_addr");

    return PyInt_AsLong(pValue);
}

// return a list of the addresses of all the data in a binary (for ARM). The first item indicates the size of the list
addr_t* get_data_addrs(void)
{
	long i;
	PyObject *pValue = python_wrap("py_func", "get_data_addrs");

	Py_ssize_t ssize = PyList_Size(pValue);
	PyObject* py_long = PyLong_FromSsize_t(ssize);
	// size could be 0
	long size = PyLong_AsLong(py_long);
	addr_t *data_addrs = (addr_t *)calloc(1+size, sizeof(addr_t));
	data_addrs[0] = size;
	for (i = 0; i < size; i++)
		data_addrs[1+i] = PyInt_AsLong(PyList_GetItem(pValue, i));
	return data_addrs;

	/*
	long int num = PyList_Size(pValue);
	addr_t *data_addr = (addr_t *)calloc(1+num, sizeof(addr_t));
	data_addr[0] = num;
	for (i = 0; i < num; i++)
		data_addr[1+i] = PyInt_AsLong(PyList_GetItem(pValue, i));
	Py_DECREF(pValue);
	return data_addr;
	*/
}

// return the entry addresses of all the functions. The fist item indicates the size of the list 
// and the second item indicates the address of the entry function
addr_t* get_symb_addrs(void)
{
	long i;
	PyObject *pValue = python_wrap("py_func", "get_symb_addrs");

	Py_ssize_t ssize = PyList_Size(pValue);
	PyObject* py_long = PyLong_FromSsize_t(ssize);
	long size = PyLong_AsLong(py_long);
	addr_t *symb_addrs = (addr_t *)calloc(1+size, sizeof(addr_t));
	symb_addrs[0] = size;
	for (i = 0; i < size; i++)
		symb_addrs[1+i] = PyInt_AsLong(PyList_GetItem(pValue, i));
	return symb_addrs;
	/*
	long int num = PyList_Size(pValue);
	addr_t *func_addr = (addr_t *)calloc(1+num, sizeof(addr_t));
	func_addr[0] = num;
	for (i = 0; i < num; i++)
		func_addr[1+i] = PyInt_AsLong(PyList_GetItem(pValue, i));
	Py_DECREF(pValue);
	return func_addr;
	*/
}
