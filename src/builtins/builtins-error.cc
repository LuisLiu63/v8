// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/builtins/builtins.h"
#include "src/builtins/builtins-utils.h"

#include "src/accessors.h"
#include "src/bootstrapper.h"
#include "src/property-descriptor.h"
#include "src/string-builder.h"

namespace v8 {
namespace internal {

// ES6 section 19.5.1.1 Error ( message )
BUILTIN(ErrorConstructor) {
  HandleScope scope(isolate);

  // 1. If NewTarget is undefined, let newTarget be the active function object,
  // else let newTarget be NewTarget.

  Handle<JSFunction> target = args.target<JSFunction>();
  Handle<JSReceiver> new_target;
  if (args.new_target()->IsJSReceiver()) {
    new_target = Handle<JSReceiver>::cast(args.new_target());
  } else {
    new_target = target;
  }

  // 2. Let O be ? OrdinaryCreateFromConstructor(newTarget, "%ErrorPrototype%",
  //    « [[ErrorData]] »).
  Handle<JSObject> err;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, err,
                                     JSObject::New(target, new_target));

  // 3. If message is not undefined, then
  //  a. Let msg be ? ToString(message).
  //  b. Let msgDesc be the PropertyDescriptor{[[Value]]: msg, [[Writable]]:
  //     true, [[Enumerable]]: false, [[Configurable]]: true}.
  //  c. Perform ! DefinePropertyOrThrow(O, "message", msgDesc).
  // 4. Return O.

  Handle<Object> msg = args.atOrUndefined(isolate, 1);
  if (!msg->IsUndefined(isolate)) {
    Handle<String> msg_string;
    ASSIGN_RETURN_FAILURE_ON_EXCEPTION(isolate, msg_string,
                                       Object::ToString(isolate, msg));
    RETURN_FAILURE_ON_EXCEPTION(
        isolate,
        JSObject::SetOwnPropertyIgnoreAttributes(
            err, isolate->factory()->message_string(), msg_string, DONT_ENUM));
  }

  // Capture the stack trace unless we're setting up.
  if (!isolate->bootstrapper()->IsActive()) {
    // Optionally capture a more detailed stack trace for the message.
    RETURN_FAILURE_ON_EXCEPTION(isolate,
                                isolate->CaptureAndSetDetailedStackTrace(err));
    // Capture a simple stack trace for the stack property.
    RETURN_FAILURE_ON_EXCEPTION(isolate,
                                isolate->CaptureAndSetSimpleStackTrace(err));
  }

  return *err;
}

// static
BUILTIN(ErrorCaptureStackTrace) {
  HandleScope scope(isolate);
  Handle<Object> object_obj = args.atOrUndefined(isolate, 1);
  if (!object_obj->IsJSObject()) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kInvalidArgument, object_obj));
  }
  Handle<JSObject> object = Handle<JSObject>::cast(object_obj);
  Handle<Object> caller = args.atOrUndefined(isolate, 2);

  // TODO(jgruber): Eagerly format the stack trace and remove accessors.h
  // include.

  // Handle writes to the global object.

  if (object->IsJSGlobalProxy()) {
    Map* map = object->map();
    if (map->has_hidden_prototype()) {
      object = handle(JSGlobalObject::cast(map->prototype()), isolate);
    }
  }

  // Check if the stack property is read-only.

  bool is_extensible = true;
  if (!JSObject::IsExtensible(object)) {
    is_extensible = false;
  }

  PropertyDescriptor desc;
  Maybe<bool> owned = JSReceiver::GetOwnPropertyDescriptor(
      isolate, object, isolate->factory()->stack_string(), &desc);
  if (owned.FromMaybe(false)) {
    if (!desc.configurable() || !desc.writable()) {
      is_extensible = false;
    }
  }

  if (!is_extensible) {
    THROW_NEW_ERROR_RETURN_FAILURE(
        isolate, NewTypeError(MessageTemplate::kDefineDisallowed,
                              isolate->factory()->stack_string(), object));
  }

  // Add stack accessors to the given object

  Handle<Map> map(object->map());
  PropertyAttributes attribs = DONT_ENUM;
  Handle<AccessorInfo> error_stack =
      Accessors::ErrorStackInfo(isolate, attribs);
  {
    AccessorConstantDescriptor d(Handle<Name>(Name::cast(error_stack->name())),
                                 error_stack, attribs);
    Handle<Map> new_map = Map::CopyInsertDescriptor(map, &d, INSERT_TRANSITION);
    JSObject::MigrateToMap(object, new_map, 1);
  }

  // Collect the stack trace.

  RETURN_FAILURE_ON_EXCEPTION(isolate,
                              isolate->CaptureAndSetDetailedStackTrace(object));
  RETURN_FAILURE_ON_EXCEPTION(
      isolate, isolate->CaptureAndSetSimpleStackTrace(object, caller));

  return *isolate->factory()->undefined_value();
}

namespace {

MaybeHandle<String> GetStringPropertyOrDefault(Isolate* isolate,
                                               Handle<JSReceiver> recv,
                                               Handle<String> key,
                                               Handle<String> default_str) {
  Handle<Object> obj;
  ASSIGN_RETURN_ON_EXCEPTION(isolate, obj, JSObject::GetProperty(recv, key),
                             String);

  Handle<String> str;
  if (obj->IsUndefined(isolate)) {
    str = default_str;
  } else {
    ASSIGN_RETURN_ON_EXCEPTION(isolate, str, Object::ToString(isolate, obj),
                               String);
  }

  return str;
}

}  // namespace

// ES6 section 19.5.3.4 Error.prototype.toString ( )
BUILTIN(ErrorPrototypeToString) {
  HandleScope scope(isolate);

  // 1. Let O be the this value.
  // 2. If Type(O) is not Object, throw a TypeError exception.
  CHECK_RECEIVER(JSReceiver, receiver, "Error.prototype.toString");

  // 3. Let name be ? Get(O, "name").
  // 4. If name is undefined, let name be "Error"; otherwise let name be
  // ? ToString(name).
  Handle<String> name_key = isolate->factory()->name_string();
  Handle<String> name_default = isolate->factory()->Error_string();
  Handle<String> name;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, name,
      GetStringPropertyOrDefault(isolate, receiver, name_key, name_default));

  // 5. Let msg be ? Get(O, "message").
  // 6. If msg is undefined, let msg be the empty String; otherwise let msg be
  // ? ToString(msg).
  Handle<String> msg_key = isolate->factory()->message_string();
  Handle<String> msg_default = isolate->factory()->empty_string();
  Handle<String> msg;
  ASSIGN_RETURN_FAILURE_ON_EXCEPTION(
      isolate, msg,
      GetStringPropertyOrDefault(isolate, receiver, msg_key, msg_default));

  // 7. If name is the empty String, return msg.
  // 8. If msg is the empty String, return name.
  if (name->length() == 0) return *msg;
  if (msg->length() == 0) return *name;

  // 9. Return the result of concatenating name, the code unit 0x003A (COLON),
  // the code unit 0x0020 (SPACE), and msg.
  IncrementalStringBuilder builder(isolate);
  builder.AppendString(name);
  builder.AppendCString(": ");
  builder.AppendString(msg);
  RETURN_RESULT_OR_FAILURE(isolate, builder.Finish());
}

}  // namespace internal
}  // namespace v8
