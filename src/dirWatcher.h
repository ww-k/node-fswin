#pragma once
#include "main.h"
#include "find.h"
#include "splitPath.h"

#define SYB_OPT_SUBDIRS (uint8_t*)"WATCH_SUB_DIRECTORIES"
#define SYB_OPT_FILESIZE (uint8_t*)"CHANGE_FILE_SIZE"
#define SYB_OPT_LASTWRITE (uint8_t*)"CHANGE_LAST_WRITE"
#define SYB_OPT_LASTACCESS (uint8_t*)"CHANGE_LAST_ACCESS"
#define SYB_OPT_CREATION (uint8_t*)"CHANGE_CREATION"
#define SYB_OPT_ATTRIBUTES (uint8_t*)"CHANGE_ATTRIBUTES"
#define SYB_OPT_SECURITY (uint8_t*)"CHANGE_SECUTITY"
#define SYB_EVT_STA (uint8_t*)"STARTED"
#define SYB_EVT_NEW (uint8_t*)"ADDED"
#define SYB_EVT_DEL (uint8_t*)"REMOVED"
#define SYB_EVT_CHG (uint8_t*)"MODIFIED"
#define SYB_EVT_REN (uint8_t*)"RENAMED"
#define SYB_EVT_MOV (uint8_t*)"MOVED"
#define SYB_EVT_REN_OLDNAME (uint8_t*)"OLD_NAME"
#define SYB_EVT_REN_NEWNAME (uint8_t*)"NEW_NAME"
#define SYB_ERR_UNABLE_TO_WATCH_SELF (uint8_t*)"UNABLE_TO_WATCH_SELF"
#define SYB_ERR_UNABLE_TO_CONTINUE_WATCHING (uint8_t*)"UNABLE_TO_CONTINUE_WATCHING"

#define SYB_BUFFERSIZE 64 * 1024

//dirWatcher requires vista or latter to call GetFinalPathNameByHandleW.
//the API is necessary since the dir we are watching could also be moved to another path.
//and it is the only way to get the new path at that kind of situation.
//however, if you still need to use dirWatcher in winxp, it will work without watching
//the parent dir. and always fire an error at start up.
class dirWatcher:ObjectWrap {
private:
	HANDLE pathhnd;
	HANDLE parenthnd;
	uv_async_t uvpathhnd;
	uv_async_t uvparenthnd;
	BOOL watchingParent;
	BOOL watchingPath;
	bool subDirs;
	DWORD options;
	wchar_t *oldName;
	wchar_t *newName;
	wchar_t *shortName;
	wchar_t *longName;
	Persistent<Function> callback;
	void *pathbuffer;
	BYTE parentbuffer[SYB_BUFFERSIZE];
public:
	dirWatcher(Handle<Object> handle, wchar_t *spath, Handle<Function> cb, bool watchSubDirs, DWORD opts):ObjectWrap() {
		Isolate *isolate = Isolate::GetCurrent();
		HandleScope scope(isolate);
		Wrap(handle);
		Ref();
		bool mute = false;
		watchingParent = watchingPath = FALSE;
		uvpathhnd = uvparenthnd = {0};
		wchar_t *realPath = oldName = newName = longName = shortName = NULL;
		parenthnd = INVALID_HANDLE_VALUE;
		callback.Reset(isolate, cb);
		pathhnd = CreateFileW(spath, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
		if (pathhnd != INVALID_HANDLE_VALUE) {
			uv_loop_t *loop = uv_default_loop();
			if (CreateIoCompletionPort(pathhnd, loop->iocp, (ULONG_PTR)pathhnd, 0)) {
				subDirs = watchSubDirs;
				options = opts;
				uv_async_init(loop, &uvpathhnd, finishWatchingPath);
				//uvpathhnd.async_req.type = UV_WAKEUP;
				//std::wcout << (uvpathhnd.async_req.data) << std::endl;
				uvpathhnd.data = this;
				beginWatchingPath(this);
				if (watchingPath) {
					uv_async_init(loop, &uvparenthnd, finishWatchingParent);
					uvparenthnd.data = this;
					realPath = getCurrentPathByHandle(pathhnd);
					if (realPath) {
						mute = watchParent(this, realPath);
						if (parenthnd != INVALID_HANDLE_VALUE) {
							beginWatchingParent(this);
						}
					}
				}
			}
		}
		if (watchingPath) {
			if (realPath) {
				callJs(this, SYB_EVT_STA, String::NewFromTwoByte(isolate, (uint16_t*)realPath));
				free(realPath);
			} else {
				callJs(this, SYB_EVT_STA, String::NewFromTwoByte(isolate, (uint16_t*)spath));
			}
			if (!mute && !watchingParent) {
				callJs(this, SYB_EVT_ERR, String::NewFromOneByte(isolate, SYB_ERR_UNABLE_TO_WATCH_SELF));
			}
		} else {
			stopWatching(this, true);
			callJs(this, SYB_EVT_ERR, String::NewFromOneByte(isolate, SYB_ERR_INITIALIZATION_FAILED));
		}
	}
	virtual ~dirWatcher() {
		callback.Reset();
		Unref();
	}
	static Handle<Function> functionRegister() {
		Isolate *isolate = Isolate::GetCurrent();
		EscapableHandleScope scope(isolate);
		Local<String> tmp;
		Local<FunctionTemplate> t = FunctionTemplate::New(isolate, New);
		t->InstanceTemplate()->SetInternalFieldCount(1);
		//set methods
		NODE_SET_PROTOTYPE_METHOD(t, "close", close);

		//set error messages
		Local<Object> errmsgs = Object::New(isolate);
		tmp = String::NewFromOneByte(isolate, SYB_ERR_UNABLE_TO_WATCH_SELF);
		errmsgs->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_ERR_UNABLE_TO_CONTINUE_WATCHING);
		errmsgs->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_ERR_INITIALIZATION_FAILED);
		errmsgs->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_ERR_WRONG_ARGUMENTS);
		errmsgs->Set(tmp, tmp, SYB_ATTR_CONST);
		t->Set(String::NewFromOneByte(isolate, SYB_ERRORS), errmsgs, SYB_ATTR_CONST);

		//set events
		Local<Object> evts = Object::New(isolate);
		tmp = String::NewFromOneByte(isolate, SYB_EVT_STA);
		evts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_EVT_END);
		evts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_EVT_NEW);
		evts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_EVT_DEL);
		evts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_EVT_REN);
		evts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_EVT_CHG);
		evts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_EVT_CHG);
		evts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_EVT_ERR);
		evts->Set(tmp, tmp, SYB_ATTR_CONST);
		t->Set(String::NewFromOneByte(isolate, SYB_EVENTS), evts, SYB_ATTR_CONST);

		//set options
		Local<Object> opts = Object::New(isolate);
		tmp = String::NewFromOneByte(isolate, SYB_OPT_SUBDIRS);
		opts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_OPT_FILESIZE);
		opts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_OPT_LASTWRITE);
		opts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_OPT_LASTACCESS);
		opts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_OPT_CREATION);
		opts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_OPT_ATTRIBUTES);
		opts->Set(tmp, tmp, SYB_ATTR_CONST);
		tmp = String::NewFromOneByte(isolate, SYB_OPT_SECURITY);
		opts->Set(tmp, tmp, SYB_ATTR_CONST);
		t->Set(String::NewFromOneByte(isolate, SYB_OPTIONS), evts, SYB_ATTR_CONST);

		return scope.Escape(t->GetFunction());
	}
private:
	static void New(const FunctionCallbackInfo<Value>& args) {
		Isolate *isolate = args.GetIsolate();
		HandleScope scope(isolate);
		Local<Value> r;
		if (args.Length() > 1 && args[0]->IsString() || args[0]->IsStringObject() && args[1]->IsFunction()) {
			if (args.IsConstructCall()) {
				DWORD options = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
				bool subDirs = true;
				if (args.Length() > 2 && args[2]->IsObject()) {
					Local<Object> iopt = Handle<Object>::Cast(args[2]);
					Local<String> tmp = String::NewFromOneByte(isolate, SYB_OPT_SUBDIRS);
					if (iopt->HasOwnProperty(tmp) && iopt->Get(tmp)->ToBoolean()->IsFalse()) {
						subDirs = false;
					}
					tmp = String::NewFromOneByte(isolate, SYB_OPT_FILESIZE);
					if (iopt->HasOwnProperty(tmp) && iopt->Get(tmp)->ToBoolean()->IsFalse()) {
						options ^= FILE_NOTIFY_CHANGE_SIZE;
					}
					tmp = String::NewFromOneByte(isolate, SYB_OPT_LASTWRITE);
					if (iopt->HasOwnProperty(tmp) && iopt->Get(tmp)->ToBoolean()->IsFalse()) {
						options ^= FILE_NOTIFY_CHANGE_LAST_WRITE;
					}
					if (iopt->Get(String::NewFromOneByte(isolate, SYB_OPT_LASTACCESS))->ToBoolean()->IsTrue()) {
						options |= FILE_NOTIFY_CHANGE_LAST_ACCESS;
					}
					if (iopt->Get(String::NewFromOneByte(isolate, SYB_OPT_CREATION))->ToBoolean()->IsTrue()) {
						options |= FILE_NOTIFY_CHANGE_CREATION;
					}
					if (iopt->Get(String::NewFromOneByte(isolate, SYB_OPT_ATTRIBUTES))->ToBoolean()->IsTrue()) {
						options |= FILE_NOTIFY_CHANGE_ATTRIBUTES;
					}
					if (iopt->Get(String::NewFromOneByte(isolate, SYB_OPT_SECURITY))->ToBoolean()->IsTrue()) {
						options |= FILE_NOTIFY_CHANGE_SECURITY;
					}
				}
				String::Value s(args[0]);
				new dirWatcher(args.This(), (wchar_t*)*s, Local<Function>::Cast(args[1]), subDirs, options);
				r = args.This();
			} else {
				if (args.Length() > 2) {
					Local<Value> v[3] = {args[0], args[1], args[2]};
					r = args.Callee()->CallAsConstructor(3, v);
				} else {
					Local<Value> v[2] = {args[0], args[1]};
					r = args.Callee()->CallAsConstructor(2, v);
				}
			}
		} else {
			r = isolate->ThrowException(Exception::Error(String::NewFromOneByte(isolate, SYB_ERR_WRONG_ARGUMENTS)));
		}
		args.GetReturnValue().Set(r);
	}
	static void close(const FunctionCallbackInfo<Value>& args) {
		Isolate *isolate = args.GetIsolate();
		EscapableHandleScope scope(isolate);
		Local<Value> result;
		dirWatcher *self = Unwrap<dirWatcher>(args.This());
		if (self->pathhnd == INVALID_HANDLE_VALUE) {
			result = False(isolate);//this method returns false if dirWatcher is failed to create or already closed
		} else {
			stopWatching(self);
			result = True(isolate);
		}
		args.GetReturnValue().Set(result);
	}
	static void savePath(dirWatcher *self, wchar_t *realPath) {
		if (self->shortName) {
			free(self->shortName);
			self->shortName = NULL;
		}
		if (self->longName) {
			free(self->longName);
			self->longName = NULL;
		}
		find::resultData *fr = find::basic(realPath);
		if (fr) {
			self->longName = _wcsdup(fr->data.cFileName);
			if (wcslen(fr->data.cAlternateFileName) > 0 && wcscmp(fr->data.cFileName, fr->data.cAlternateFileName) != 0) {
				self->shortName = _wcsdup(fr->data.cAlternateFileName);
			}
			delete fr;
		}
	}
	static bool watchParent(dirWatcher *self, wchar_t *realPath) {//return true means no need to fire UNABLE_TO_WATCH_SELF
		bool result = false;
		if (self->parenthnd != INVALID_HANDLE_VALUE) {
			CloseHandle(self->parenthnd);
			self->parenthnd = INVALID_HANDLE_VALUE;
		}
		splitPath::splitedPath *sp = splitPath::basic(realPath);
		if (sp) {
			if (sp->parentLen == 0) {
				result = true;
			} else {
				savePath(self, realPath);
				if (self->longName) {
					wchar_t *parent = new wchar_t[sp->parentLen + 1];
					wcsncpy_s(parent, sp->parentLen + 1, realPath, sp->parentLen);
					delete sp;
					self->parenthnd = CreateFileW(parent, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
					delete parent;
					if (self->parenthnd != INVALID_HANDLE_VALUE && !CreateIoCompletionPort(self->parenthnd, uv_default_loop()->iocp, (ULONG_PTR)self->parenthnd, 0)) {
						CloseHandle(self->parenthnd);
						self->parenthnd = INVALID_HANDLE_VALUE;
					}
				}
			}
		}
		return result;
	}
	static void beginWatchingParent(dirWatcher *self) {
		self->watchingParent = ReadDirectoryChangesW(self->parenthnd, self->parentbuffer, SYB_BUFFERSIZE, FALSE, FILE_NOTIFY_CHANGE_DIR_NAME, NULL, &self->uvparenthnd.async_req.overlapped, NULL);
	}
	static void beginWatchingPath(dirWatcher *self) {
		self->pathbuffer = malloc(SYB_BUFFERSIZE);
		if (self->pathbuffer) {
			self->watchingPath = ReadDirectoryChangesW(self->pathhnd, self->pathbuffer, SYB_BUFFERSIZE, self->subDirs, self->options, NULL, &self->uvpathhnd.async_req.overlapped, NULL);
			if (!self->watchingPath) {
				free(self->pathbuffer);
			}
		}
	}
	static void finishWatchingParent(uv_async_t *hnd) {
		dirWatcher *self = (dirWatcher*)hnd->data;
		self->watchingParent = FALSE;
		if (self->parenthnd == INVALID_HANDLE_VALUE) {
			uv_close((uv_handle_t*)&hnd, NULL);
			checkWatchingStoped(self);
		} else {
			Isolate *isolate = Isolate::GetCurrent();
			HandleScope scope(isolate);
			if (hnd->async_req.overlapped.Internal == ERROR_SUCCESS) {
				wchar_t *newpath = NULL;
				FILE_NOTIFY_INFORMATION *pInfo;
				DWORD d = 0;
				do {
					pInfo = (FILE_NOTIFY_INFORMATION*)((ULONG_PTR)self->parentbuffer + d);
					if ((pInfo->Action == FILE_ACTION_RENAMED_OLD_NAME || pInfo->Action == FILE_ACTION_REMOVED) && wcsncmp(self->longName, pInfo->FileName, MAX(pInfo->FileNameLength / sizeof(wchar_t), wcslen(self->longName))) == 0 || (self->shortName && wcsncmp(self->shortName, pInfo->FileName, MAX(pInfo->FileNameLength / sizeof(wchar_t), wcslen(self->shortName))) == 0)) {
						newpath = getCurrentPathByHandle(self->pathhnd);
						//std::wcout << 123 << std::endl;
						if (pInfo->Action == FILE_ACTION_RENAMED_OLD_NAME) {
							savePath(self, newpath);
							if (!self->longName) {
								CloseHandle(self->parenthnd);
								self->parenthnd = INVALID_HANDLE_VALUE;
							}
						} else{
							watchParent(self, newpath);
						}
						break;
					}
					d += pInfo->NextEntryOffset;
				} while (pInfo->NextEntryOffset > 0);
				if (self->parenthnd != INVALID_HANDLE_VALUE) {
					beginWatchingParent(self);
				}
				if (newpath) {
					callJs(self, SYB_EVT_MOV, String::NewFromTwoByte(isolate, (uint16_t*)newpath));
					free(newpath);
				}
			}
			if (!self->watchingParent) {
				stopWatchingParent(self);
				callJs(self, SYB_EVT_ERR, String::NewFromOneByte(isolate, SYB_ERR_UNABLE_TO_WATCH_SELF));
			}
		}
	}
	static void finishWatchingPath(uv_async_t *hnd) {
		dirWatcher *self = (dirWatcher*)hnd->data;
		self->watchingPath = FALSE;
		if (self->pathhnd == INVALID_HANDLE_VALUE) {
			uv_close((uv_handle_t*)&hnd, NULL);
			free(self->pathbuffer);
			checkWatchingStoped(self);
		} else {
			Isolate *isolate = Isolate::GetCurrent();
			HandleScope scope(isolate);
			bool e = false;
			if (hnd->async_req.overlapped.Internal == ERROR_SUCCESS) {
				FILE_NOTIFY_INFORMATION *pInfo;
				void *buffer = self->pathbuffer;
				beginWatchingPath(self);
				DWORD d = 0;
				do {
					pInfo = (FILE_NOTIFY_INFORMATION*)((ULONG_PTR)buffer + d);
					Local<String> filename = String::NewFromTwoByte(isolate, (uint16_t*)pInfo->FileName, String::kNormalString, pInfo->FileNameLength / sizeof(wchar_t));
					if (pInfo->Action == FILE_ACTION_ADDED) {
						callJs(self, SYB_EVT_NEW, filename);
					} else if (pInfo->Action == FILE_ACTION_REMOVED) {
						callJs(self, SYB_EVT_DEL, filename);
					} else if (pInfo->Action == FILE_ACTION_MODIFIED) {
						callJs(self, SYB_EVT_CHG, filename);
					} else {
						if (pInfo->Action == FILE_ACTION_RENAMED_OLD_NAME) {
							if (self->newName) {
								Local<Object> arg = Object::New(isolate);
								arg->Set(String::NewFromOneByte(isolate, SYB_EVT_REN_OLDNAME), filename);
								arg->Set(String::NewFromOneByte(isolate, SYB_EVT_REN_NEWNAME), String::NewFromTwoByte(isolate, (uint16_t*)self->newName));
								delete self->newName;
								self->newName = NULL;
								callJs(self, SYB_EVT_REN, arg);
							} else {
								size_t sz = pInfo->FileNameLength + 1;
								self->oldName = new wchar_t[sz];
								wcscpy_s(self->oldName, sz, pInfo->FileName);
							}
						} else if (pInfo->Action == FILE_ACTION_RENAMED_NEW_NAME) {
							if (self->oldName) {
								Local<Object> arg = Object::New(isolate);
								arg->Set(String::NewFromOneByte(isolate, SYB_EVT_REN_OLDNAME), filename);
								arg->Set(String::NewFromOneByte(isolate, SYB_EVT_REN_NEWNAME), String::NewFromTwoByte(isolate, (uint16_t*)self->oldName));
								delete self->oldName;
								self->oldName = NULL;
								callJs(self, SYB_EVT_REN, arg);
							} else {
								size_t sz = pInfo->FileNameLength + 1;
								self->newName = new wchar_t[sz];
								wcscpy_s(self->newName, sz, pInfo->FileName);
							}
						}
					}
					d += pInfo->NextEntryOffset;
				} while (pInfo->NextEntryOffset > 0);
				free(buffer);
			}
			if (!self->watchingPath) {
				stopWatching(self);
				callJs(self, SYB_EVT_ERR, String::NewFromOneByte(isolate, SYB_ERR_UNABLE_TO_CONTINUE_WATCHING));
			}
		}
	}
	static void stopWatching(dirWatcher *self, bool mute=false) {
		if (self->pathhnd != INVALID_HANDLE_VALUE) {
			CloseHandle(self->pathhnd);
			self->pathhnd = INVALID_HANDLE_VALUE;
		}
		if (self->newName) {
			delete self->newName;
			self->newName = NULL;
		}
		if (self->oldName) {
			delete self->oldName;
			self->oldName = NULL;
		}
		if (!self->watchingPath && uv_is_active((uv_handle_t*)&self->uvpathhnd)) {
			uv_close((uv_handle_t*)&self->uvpathhnd, NULL);
		}
		stopWatchingParent(self);
		if (!mute) {
			checkWatchingStoped(self);
		}
	}
	static void stopWatchingParent(dirWatcher *self) {
		if (self->parenthnd != INVALID_HANDLE_VALUE) {
			CloseHandle(self->parenthnd);
			self->parenthnd = INVALID_HANDLE_VALUE;
		}
		if (self->longName) {
			free(self->longName);
			self->longName = NULL;
		}
		if (self->shortName) {
			free(self->shortName);
			self->longName = NULL;
		}
		if (!self->watchingParent && uv_is_active((uv_handle_t*)&self->uvparenthnd)) {
			uv_close((uv_handle_t*)&self->uvparenthnd, NULL);
		}
	}
	static void checkWatchingStoped(dirWatcher *self) {
		if (!uv_is_active((uv_handle_t*)&self->uvpathhnd) && !uv_is_active((uv_handle_t*)&self->uvparenthnd)) {
			Isolate *isolate = Isolate::GetCurrent();
			HandleScope scope(isolate);
			callJs(self, SYB_EVT_END, Undefined(isolate));
		}
	}
	static void callJs(dirWatcher *self, uint8_t *evt_type, Handle<Value> src) {
		Isolate *isolate = Isolate::GetCurrent();
		HandleScope scope(isolate);
		Local<Value> arg[2] = {String::NewFromOneByte(isolate, evt_type), src};
		Local<Function> callback = Local<Function>::New(isolate, self->callback);
		callback->Call(Local<Object>::New(isolate, self->persistent()), 2, arg);
	}
};