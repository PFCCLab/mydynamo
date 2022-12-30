#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <frameobject.h>
#include <pystate.h>

// see https://bugs.python.org/issue35886
#if PY_VERSION_HEX >= 0x03080000
#define Py_BUILD_CORE
#include "internal/pycore_pystate.h"
#undef Py_BUILD_CORE
#endif

//#define TORCHDYNAMO_DEBUG
#define unlikely(x) __builtin_expect((x), 0)

#define NULL_CHECK(val)                                                        \
  if (unlikely((val) == NULL)) {                                               \
    fprintf(stderr, "NULL ERROR: %s:%d\n", __FILE__, __LINE__);                \
    PyErr_Print();                                                             \
    abort();                                                                   \
  } else {                                                                     \
  }

#define CHECK(cond)                                                            \
  if (unlikely(!(cond))) {                                                     \
    fprintf(stderr, "DEBUG CHECK FAILED: %s:%d\n", __FILE__, __LINE__);        \
    abort();                                                                   \
  } else {                                                                     \
  }

#ifdef TORCHDYNAMO_DEBUG

#define DEBUG_CHECK(cond) CHECK(cond)
#define DEBUG_NULL_CHECK(val) NULL_CHECK(val)
#define DEBUG_TRACE(msg, ...)                                                  \
  fprintf(stderr, "TRACE[%s:%d] " msg "\n", __func__, __LINE__, __VA_ARGS__)
#define DEBUG_TRACE0(msg)                                                      \
  fprintf(stderr, "TRACE[%s:%d] " msg "\n", __func__, __LINE__)

#else

#define DEBUG_CHECK(cond)
#define DEBUG_NULL_CHECK(val)
#define DEBUG_TRACE(msg, ...)
#define DEBUG_TRACE0(msg)

#endif

// Flag to just run a frame normally
#define SKIP_CODE ((void *)0x1)

static PyObject *noargs = NULL;     /* cached empty tuple */
static PyObject *dotzerokey = NULL; /* ".0" */

size_t extra_index = -1;

static PyObject *eval_frame_callback_ = NULL;

inline static PyObject *eval_frame_callback_get(void) {
  return eval_frame_callback_;
}

inline static void eval_frame_callback_set(PyObject *obj) {
  eval_frame_callback_ = obj;
}

static void ignored(void *obj) {}
static PyObject *_custom_eval_frame(PyThreadState *tstate,
                                    PyFrameObject *frame,
                                    int throw_flag);
#if PY_VERSION_HEX >= 0x03090000
static PyObject *custom_eval_frame(PyThreadState *tstate,
                                   PyFrameObject *frame,
                                   int throw_flag) {
  return _custom_eval_frame(tstate, frame, throw_flag);
}

#else
static PyObject *custom_eval_frame(PyFrameObject *frame, int throw_flag) {
  PyThreadState *tstate = PyThreadState_GET();
  return _custom_eval_frame(tstate, frame, throw_flag);
}
#endif

inline static PyObject *eval_frame_default(PyThreadState *tstate,
                                           PyFrameObject *frame,
                                           int throw_flag) {
  #if PY_VERSION_HEX >= 0x03090000
  if (tstate == NULL) {
    tstate = PyThreadState_GET();
  }
  return _PyEval_EvalFrameDefault(tstate, frame, throw_flag);
  #else
  return _PyEval_EvalFrameDefault(frame, throw_flag);
  #endif
}

inline static void enable_eval_frame(PyThreadState *tstate) {
  
  DEBUG_TRACE0("enable_eval_frame ");

  #if PY_VERSION_HEX >= 0x03090000
  _PyInterpreterState_SetEvalFrameFunc(tstate->interp, &custom_eval_frame);
  #else
  tstate->interp->eval_frame = &custom_eval_frame;
  #endif
}

inline static void disable_eval_frame(PyThreadState *tstate) {
  #if PY_VERSION_HEX >= 0x03090000
  _PyInterpreterState_SetEvalFrameFunc(tstate->interp, &_PyEval_EvalFrameDefault);
  #else
  tstate->interp->eval_frame = &_PyEval_EvalFrameDefault;
  #endif
}

static inline PyObject *call_callback(PyObject *callable, PyObject *frame,
                                      long cache_len) {
  PyObject *args = Py_BuildValue("(Ol)", frame, cache_len);
  NULL_CHECK(args);
  
  DEBUG_TRACE("calling callback %p with args %p, %ld", callable, frame, cache_len);

  PyObject *result = PyObject_CallObject(callable, args);
  Py_DECREF(args);
  return result;
}

typedef struct cache_entry {
  // check the guards: lambda: <locals of user function>: bool
  PyObject *check_fn;
  // modified user bytecode (protected by check_fn's guards)
  PyCodeObject *code;
  // on a cache miss, linked list of next thing to try
  struct cache_entry *next;
} CacheEntry;

static CacheEntry *create_cache_entry(CacheEntry *next,
                                      PyObject *guarded_code) {
  CacheEntry *e = (CacheEntry *)malloc(sizeof(CacheEntry));
  DEBUG_NULL_CHECK(e);
  e->check_fn = PyObject_GetAttrString(guarded_code, "check_fn");
  NULL_CHECK(e->check_fn);
  e->code = (PyCodeObject *)PyObject_GetAttrString(guarded_code, "code");
  NULL_CHECK(e->code);
  e->next = next;
  return e;
}

#ifdef TORCHDYNAMO_DEBUG
inline static const char *name(PyFrameObject *frame) {
  DEBUG_CHECK(PyUnicode_Check(frame->f_code->co_name));
  return PyUnicode_AsUTF8(frame->f_code->co_name);
}
#endif

static long cache_size(CacheEntry *e) {
  if (e == NULL) {
    return 0;
  }
  return 1 + cache_size(e->next);
}

inline static CacheEntry *get_extra(PyCodeObject *code) {
  CacheEntry *extra = NULL;
  _PyCode_GetExtra((PyObject *)code, extra_index, (void *)&extra);
  return extra;
}

inline static void set_extra(PyCodeObject *code, CacheEntry *extra) {
  // TODO(jansel): would it be faster to bypass this?
  _PyCode_SetExtra((PyObject *)code, extra_index, extra);
}

inline static PyObject *eval_custom_code(PyThreadState *tstate,
                                         PyFrameObject *frame,
                                         PyCodeObject *code, int throw_flag) {
  Py_ssize_t ncells = 0;
  Py_ssize_t nfrees = 0;
  Py_ssize_t nlocals_new = code->co_nlocals;
  Py_ssize_t nlocals_old = frame->f_code->co_nlocals;

  if ((code->co_flags & CO_NOFREE) == 0) {
    ncells = PyTuple_GET_SIZE(code->co_cellvars);
    nfrees = PyTuple_GET_SIZE(code->co_freevars);
  }

  DEBUG_NULL_CHECK(tstate);
  DEBUG_NULL_CHECK(frame);
  DEBUG_NULL_CHECK(code);
  DEBUG_CHECK(ncells == PyTuple_GET_SIZE(frame->f_code->co_cellvars));
  DEBUG_CHECK(nfrees == PyTuple_GET_SIZE(frame->f_code->co_freevars));
  DEBUG_CHECK(nlocals_new >= nlocals_old);

  PyFrameObject *shadow =
      PyFrame_New(tstate, code, frame->f_globals, NULL);
  if (shadow == NULL) {
    return NULL;
  }

  PyObject **fastlocals_old = frame->f_localsplus;
  PyObject **fastlocals_new = shadow->f_localsplus;

  for (Py_ssize_t i = 0; i < nlocals_old; i++) {
    Py_XINCREF(fastlocals_old[i]);
    fastlocals_new[i] = fastlocals_old[i];
  }

  for (Py_ssize_t i = 0; i < ncells + nfrees; i++) {
    Py_XINCREF(fastlocals_old[nlocals_old + i]);
    fastlocals_new[nlocals_new + i] = fastlocals_old[nlocals_old + i];
  }

  PyObject *result = eval_frame_default(tstate, shadow, throw_flag);
  Py_DECREF(shadow);
  return result;
}

static PyObject *_custom_eval_frame(PyThreadState *tstate,
                                    PyFrameObject *frame,
                                    int throw_flag) {
  DEBUG_TRACE("begin %s %s %i %i %i %i", name(frame),
              PyUnicode_AsUTF8(frame->f_code->co_filename), frame->f_lineno,
              frame->f_lasti, frame->f_iblock, frame->f_executing);
  CacheEntry *extra = get_extra(frame->f_code);
  if (extra == SKIP_CODE) {
    DEBUG_TRACE("skip %s", name(frame));
    return eval_frame_default(tstate, frame, throw_flag);
  }
  if (PyFrame_FastToLocalsWithError(frame) < 0) {
    DEBUG_TRACE("error %s", name(frame));
    return NULL;
  }
  DEBUG_CHECK(PyDict_CheckExact(frame->f_locals));
  DEBUG_CHECK(PyDict_CheckExact(frame->f_globals));
  DEBUG_CHECK(PyDict_CheckExact(frame->f_builtins));

  // don't run custom_eval_frame() for guard function
  PyObject *callback = eval_frame_callback_get();
  disable_eval_frame(tstate);

  PyObject *result =
      call_callback(callback, (PyObject *)frame, cache_size(extra));
  if (result == NULL) {
    // internal exception, returning here will leak the exception into user code
    // this is useful for debugging -- but we dont want it to happen outside of
    // testing
    return NULL;
  } else if (result != Py_None) {
    DEBUG_TRACE("create cache %s", name(frame));
    extra = create_cache_entry(extra, result);
    Py_DECREF(result);
    set_extra(frame->f_code, extra);
    enable_eval_frame(tstate);
    return eval_custom_code(tstate, frame, extra->code, throw_flag);
  } else {
    DEBUG_TRACE("create skip %s", name(frame));
    Py_DECREF(result);
    set_extra(frame->f_code, SKIP_CODE);
    enable_eval_frame(tstate);
    return eval_frame_default(tstate, frame, throw_flag);
  }
}

static PyObject *set_eval_frame(PyObject *new_callback, PyThreadState *tstate) {
  // Change the eval frame callback and return the old one
  //  - Python callable(): enables TorchDynamo

  PyObject *old_callback;
  #if PY_VERSION_HEX >= 0x03090000
  void *old_eval_frame = _PyInterpreterState_GetEvalFrameFunc(tstate->interp);
  #else
  void *old_eval_frame = tstate->interp->eval_frame;
  #endif
  
  if (old_eval_frame == &custom_eval_frame) {
    old_callback = eval_frame_callback_get();
  } else if (old_eval_frame == &_PyEval_EvalFrameDefault) {
    old_callback = Py_None;
  } else {
    CHECK(0);
  }

  // owned by caller
  Py_INCREF(old_callback);

  if (new_callback == Py_None) {
    disable_eval_frame(tstate);
  } else {
    enable_eval_frame(tstate);
    Py_INCREF(new_callback);
    Py_DECREF(eval_frame_callback_get());
    eval_frame_callback_set(new_callback);
    DEBUG_TRACE("eval_frame_callback_set %p", new_callback);
  }

  return old_callback;
}

static PyObject *set_eval_frame_py(PyObject *dummy, PyObject *args) {
  // arg: dummy
  //  Change the eval frame callback and return the old one
  //  - None: disables dynamo
  //  - Python callable(): enables dynamo

  PyObject *callback = NULL;
  if (!PyArg_ParseTuple(args, "O:callback", &callback)) {
    DEBUG_TRACE0("arg error");
    return NULL;
  }
  if (callback != Py_None && !PyCallable_Check(callback)) {
    DEBUG_TRACE0("arg error");
    PyErr_SetString(PyExc_TypeError, "expected a callable");
    return NULL;
  }

  DEBUG_TRACE(
    "python enabled=%d", 
    callback != Py_None);

  return set_eval_frame(callback, PyThreadState_GET());
}

static PyMethodDef _methods[] = {
    {"set_eval_frame", set_eval_frame_py, METH_VARARGS, NULL},
    {NULL, NULL, 0, NULL}};

static struct PyModuleDef _module = {
    PyModuleDef_HEAD_INIT, "_eval_frame",
    "Module containing hooks to override eval_frame", -1, _methods};

PyMODINIT_FUNC PyInit__eval_frame(void) {
  CHECK(sizeof(unsigned long) == sizeof(void *));
  extra_index = _PyEval_RequestCodeExtraIndex(ignored);

  Py_INCREF(Py_None);
  eval_frame_callback_set(Py_None);

  noargs = PyTuple_New(0);
  dotzerokey = PyUnicode_InternFromString(".0");
  return PyModule_Create(&_module);
}
