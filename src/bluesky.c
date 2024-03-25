#include <network.h>
#include <server.h>
#include <bluesky_timer.h>

static int bluesky_exec(PyObject *module)
{
    PyModule_AddObject(module, "network", PyInit_network());
    PyModule_AddObject(module, "server", PyInit_server());
    PyModule_AddObject(module, "timer", PyInit_timer());
    return 0;
}

static PyModuleDef_Slot bluesky_slots[] = {
    {Py_mod_exec, bluesky_exec},
    {0, NULL}};

static PyMethodDef blusky_methods[] = {
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef blusky_module =
    {
        PyModuleDef_HEAD_INIT,
        "blusky",                                                                /* name of module */
        "usage: Combinations.uniqueCombinations(lstSortableItems, comboSize)\n", /* module documentation, may be NULL */
        0,                                                                       /* size of per-interpreter state of the module, or -1 if the module keeps state in global variables. */
        blusky_methods,
        bluesky_slots};

PyMODINIT_FUNC PyInit_bluesky()
{
    return PyModuleDef_Init(&blusky_module);
};
