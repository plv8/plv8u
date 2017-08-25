/*-------------------------------------------------------------------------
 *
 * plv8_func.cc : PL/v8 built-in functions.
 *
 * Copyright (c) 2009-2012, the PLV8JS Development Group.
 *-------------------------------------------------------------------------
 */
#include "plv8.h"
#include "plv8_param.h"
#include "helpers/file.h"
#include <string>
#include <sstream>

extern "C" {
#include "access/xact.h"
#include "catalog/pg_type.h"
#include "executor/spi.h"
#include "parser/parse_type.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"

#include <errno.h>
#include <fcntl.h>
} // extern "C"

using namespace v8;

extern v8::Isolate* plv8_isolate;
static void plv8u_StatFile(const FunctionCallbackInfo<v8::Value>& args);
static void plv8u_ReadFile(const FunctionCallbackInfo<v8::Value>& args);
static void plv8_FunctionInvoker(const FunctionCallbackInfo<v8::Value>& args) throw();



static inline Local<v8::Value>
WrapCallback(FunctionCallback func)
{
	return External::New(plv8_isolate,
			reinterpret_cast<void *>(
				reinterpret_cast<uintptr_t>(func)));
}

static inline FunctionCallback
UnwrapCallback(Handle<v8::Value> value)
{
	return reinterpret_cast<FunctionCallback>(
			reinterpret_cast<uintptr_t>(External::Cast(*value)->Value()));
}

static inline void
SetCallback(Handle<ObjectTemplate> obj, const char *name,
			FunctionCallback func, PropertyAttribute attr = None)
{
	obj->Set(String::NewFromUtf8(plv8_isolate, name, String::kInternalizedString),
				FunctionTemplate::New(plv8_isolate, plv8_FunctionInvoker,
					WrapCallback(func)), attr);
}

void
SetupPlv8uFunctions(Handle<ObjectTemplate> plv8u)
{
	PropertyAttribute	attrFull =
		PropertyAttribute(ReadOnly | DontEnum | DontDelete);

	Local<ObjectTemplate> internal = ObjectTemplate::New(plv8_isolate);

	Local<ObjectTemplate> fs = ObjectTemplate::New(plv8_isolate);
	SetCallback(fs, "stat", plv8u_StatFile, attrFull);
	SetCallback(fs, "readFile", plv8u_ReadFile, attrFull);

	internal->Set(String::NewFromUtf8(plv8_isolate, "fs"), fs);
	plv8u->Set(String::NewFromUtf8(plv8_isolate, "_internal"), internal);

	plv8u->SetInternalFieldCount(PLV8_INTNL_MAX);
}

/*
 * v8 is not exception-safe! We cannot throw C++ exceptions over v8 functions.
 * So, we catch C++ exceptions and convert them to JavaScript ones.
 */
static void
plv8_FunctionInvoker(const FunctionCallbackInfo<v8::Value> &args) throw()
{
	HandleScope		handle_scope(plv8_isolate);
	MemoryContext	ctx = CurrentMemoryContext;
	FunctionCallback	fn = UnwrapCallback(args.Data());

	try
	{
		return fn(args);
	}
	catch (js_error& e)
	{
		args.GetReturnValue().Set(plv8_isolate->ThrowException(e.error_object()));
	}
	catch (pg_error& e)
	{
		MemoryContextSwitchTo(ctx);
		ErrorData *edata = CopyErrorData();

		Handle<String> message = ToString(edata->message);
		Handle<String> sqlerrcode = ToString(unpack_sql_state(edata->sqlerrcode));
#if PG_VERSION_NUM >= 90300
		Handle<Primitive> schema_name = edata->schema_name ?
			Handle<Primitive>(ToString(edata->schema_name)) : Null(plv8_isolate);
		Handle<Primitive> table_name = edata->table_name ?
			Handle<Primitive>(ToString(edata->table_name)) : Null(plv8_isolate);
		Handle<Primitive> column_name = edata->column_name ?
			Handle<Primitive>(ToString(edata->column_name)) : Null(plv8_isolate);
		Handle<Primitive> datatype_name = edata->datatype_name ?
			Handle<Primitive>(ToString(edata->datatype_name)) : Null(plv8_isolate);
		Handle<Primitive> constraint_name = edata->constraint_name ?
			Handle<Primitive>(ToString(edata->constraint_name)) : Null(plv8_isolate);
		Handle<Primitive> detail = edata->detail ?
			Handle<Primitive>(ToString(edata->detail)) : Null(plv8_isolate);
		Handle<Primitive> hint = edata->hint ?
			Handle<Primitive>(ToString(edata->hint)) : Null(plv8_isolate);
		Handle<Primitive> context = edata->context ?
			Handle<Primitive>(ToString(edata->context)) : Null(plv8_isolate);
		Handle<Primitive> internalquery = edata->internalquery ?
			Handle<Primitive>(ToString(edata->internalquery)) : Null(plv8_isolate);
		Handle<Integer> code = Uint32::New(plv8_isolate, edata->sqlerrcode);

#endif

		FlushErrorState();
		FreeErrorData(edata);

		Handle<v8::Object> err = Exception::Error(message)->ToObject();
		err->Set(String::NewFromUtf8(plv8_isolate, "sqlerrcode"), sqlerrcode);
#if PG_VERSION_NUM >= 90300
		err->Set(String::NewFromUtf8(plv8_isolate, "schema_name"), schema_name);
		err->Set(String::NewFromUtf8(plv8_isolate, "table_name"), table_name);
		err->Set(String::NewFromUtf8(plv8_isolate, "column_name"), column_name);
		err->Set(String::NewFromUtf8(plv8_isolate, "datatype_name"), datatype_name);
		err->Set(String::NewFromUtf8(plv8_isolate, "constraint_name"), constraint_name);
		err->Set(String::NewFromUtf8(plv8_isolate, "detail"), detail);
		err->Set(String::NewFromUtf8(plv8_isolate, "hint"), hint);
		err->Set(String::NewFromUtf8(plv8_isolate, "context"), context);
		err->Set(String::NewFromUtf8(plv8_isolate, "internalquery"), internalquery);
		err->Set(String::NewFromUtf8(plv8_isolate, "code"), code);
#endif

		args.GetReturnValue().Set(plv8_isolate->ThrowException(err));
	}
}

static void plv8u_StatFile(const FunctionCallbackInfo<v8::Value>& args) {
	Isolate* plv8_isolate = args.GetIsolate();
	if (args.Length() < 1) {
	    // Throw an Error that is passed back to JavaScript
	    plv8_isolate->ThrowException(Exception::TypeError(
	        String::NewFromUtf8(plv8_isolate, "path must be a string")));
	    return;
  }

  // Check the argument types
  if (!args[0]->IsString()) {
	    plv8_isolate->ThrowException(Exception::TypeError(
	        String::NewFromUtf8(plv8_isolate, "path must be a string")));
	    return;
  }

	v8::String::Utf8Value s(args[0]);
	std::string str(*s);
	struct plv8u_file_status *status = stat_file(str.c_str());

	if (status->error) {
		std::stringstream ss;
		ss << "unable to open file or directory: '" << str.c_str() << "' " << strerror(status->error);
		std::string ns = ss.str();
		plv8_isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(plv8_isolate, ns.c_str())
		));

		return;
	}

	Local<Object> object = Object::New(plv8_isolate);

	object->Set(String::NewFromUtf8(plv8_isolate, "st_dev"), Number::New(plv8_isolate, status->stat_buf->st_dev));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_ino"), Number::New(plv8_isolate, status->stat_buf->st_ino));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_mode"), Number::New(plv8_isolate, status->stat_buf->st_mode));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_nlink"), Number::New(plv8_isolate, status->stat_buf->st_nlink));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_uid"), Number::New(plv8_isolate, status->stat_buf->st_uid));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_gid"), Number::New(plv8_isolate, status->stat_buf->st_gid));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_atimespec"), Number::New(plv8_isolate, status->stat_buf->st_atimespec.tv_nsec));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_mtimespec"), Number::New(plv8_isolate, status->stat_buf->st_mtimespec.tv_nsec));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_ctimespec"), Number::New(plv8_isolate, status->stat_buf->st_ctimespec.tv_nsec));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_size"), Number::New(plv8_isolate, status->stat_buf->st_size));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_blocks"), Number::New(plv8_isolate, status->stat_buf->st_blocks));
	object->Set(String::NewFromUtf8(plv8_isolate, "st_blksize"), Number::New(plv8_isolate, status->stat_buf->st_blksize));

	pfree(status);

	args.GetReturnValue().Set(object);
}

static void plv8u_ReadFile(const FunctionCallbackInfo<v8::Value>& args) {
	Isolate* plv8_isolate = args.GetIsolate();
	if (args.Length() < 1) {
	    // Throw an Error that is passed back to JavaScript
	    plv8_isolate->ThrowException(Exception::TypeError(
	        String::NewFromUtf8(plv8_isolate, "path must be a string")));
	    return;
  }

  // Check the argument types
  if (!args[0]->IsString()) {
	    plv8_isolate->ThrowException(Exception::TypeError(
	        String::NewFromUtf8(plv8_isolate, "path must be a string")));
	    return;
  }

	v8::String::Utf8Value s(args[0]);
	std::string str(*s);
	struct plv8u_file_status *status = read_file(str.c_str());

	if (status->error) {
		std::stringstream ss;
		ss << "unable to open file or directory: '" << str.c_str() << "' " << strerror(status->error);
		std::string ns = ss.str();
		plv8_isolate->ThrowException(Exception::Error(
			String::NewFromUtf8(plv8_isolate, ns.c_str())
		));

		return;
	}

	Local<String> ret = String::NewFromUtf8(plv8_isolate, (const char *) status->contents);

	pfree(status->contents);
	pfree(status);

	args.GetReturnValue().Set(ret);
}

#if 0
// work in progress, hence defined off
static void plv8u_OpenFile(const FunctionCallbackInfo<v8::Value>& args) {
	Isolate* plv8_isolate = args.GetIsolate();
	if (args.Length() < 2) {
			// Throw an Error that is passed back to JavaScript
			plv8_isolate->ThrowException(Exception::Error(
					String::NewFromUtf8(plv8_isolate, "Unkown file open flag: undefined")));
			return;
	}

	// Check the argument types
	if (!args[0]->IsString()) {
			plv8_isolate->ThrowException(Exception::TypeError(
					String::NewFromUtf8(plv8_isolate, "path must be a string")));
			return;
	}

	if (!args[1]->IsString()) {
			plv8_isolate->ThrowException(Exception::TypeError(
					String::NewFromUtf8(plv8_isolate, "flag must be a string")));
			return;
	}

	// path
	v8::String::Utf8Value s(args[0]);
	std::string path(*s);

	// flags
	v8::String::Utf8Value s2(args[1]);
	std::string flags(*s2);

	// mode, default to 0666
	int mode = 0666;

	if (args.Length() == 3) {
		mode = args[2]->NumberValue();
	}

	int file = open(path.c_str(), )
}
#endif
