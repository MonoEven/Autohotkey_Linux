#define _GNU_SOURCE
#include <dbus/dbus.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static const char *g_key = "1";
static char g_session[512] = {0};   /* real session handle */
static int g_response_code = -99;

static void print_iter(DBusMessageIter *it, int depth)
{
	int t = dbus_message_iter_get_arg_type(it);
	if (t == DBUS_TYPE_STRING || t == DBUS_TYPE_OBJECT_PATH || t == DBUS_TYPE_SIGNATURE)
	{ const char *s=0; dbus_message_iter_get_basic(it,&s); printf("%*sS: %s\n", depth*2,"",s?s:"(n)"); }
	else if (t == DBUS_TYPE_UINT32) { dbus_uint32_t v=0; dbus_message_iter_get_basic(it,&v); printf("%*su32: %u\n", depth*2,"",(unsigned)v); }
	else if (t == DBUS_TYPE_UINT64) { dbus_uint64_t v=0; dbus_message_iter_get_basic(it,&v); printf("%*su64: %llu\n", depth*2,"",(unsigned long long)v); }
	else if (dbus_type_is_container(t))
	{ DBusMessageIter sub; dbus_message_iter_recurse(it,&sub); printf("%*s[%c]\n", depth*2,"",t);
	  while (dbus_message_iter_get_arg_type(&sub)!=DBUS_TYPE_INVALID){ print_iter(&sub,depth+1); dbus_message_iter_next(&sub); } }
}

/* Extract results["session_handle"] from an a{sv} iterator. */
static void extract_session(DBusMessageIter *dict)
{
	while (dbus_message_iter_get_arg_type(dict) == DBUS_TYPE_DICT_ENTRY)
	{
		DBusMessageIter de = *dict, var;
		dbus_message_iter_recurse(dict, &de);
		const char *key = NULL;
		dbus_message_iter_get_basic(&de, &key);
		dbus_message_iter_next(&de);
		if (key && !strcmp(key, "session_handle"))
		{
			if (dbus_message_iter_get_arg_type(&de) == DBUS_TYPE_VARIANT)
			{
				dbus_message_iter_recurse(&de, &var);
				if (dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_STRING ||
				    dbus_message_iter_get_arg_type(&var) == DBUS_TYPE_OBJECT_PATH)
				{
					const char *v = NULL;
					dbus_message_iter_get_basic(&var, &v);
					if (v) snprintf(g_session, sizeof(g_session), "%s", v);
				}
			}
		}
		dbus_message_iter_next(dict);
	}
}

static DBusHandlerResult filter(DBusConnection *c, DBusMessage *m, void *ud)
{
	const char *iface = dbus_message_get_interface(m);
	const char *member = dbus_message_get_member(m);
	if (!iface) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	if (member && !strcmp(member, "Response"))
	{
		printf("Response path=%s\n", dbus_message_get_path(m));
		DBusMessageIter it; dbus_message_iter_init(m, &it);
		if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_UINT32)
		{ dbus_uint32_t r=0; dbus_message_iter_get_basic(&it,&r); g_response_code=(int)r; printf("  response_code=%u\n",(unsigned)r); }
		dbus_message_iter_next(&it);
		printf("  results:\n");
		print_iter(&it, 2);
		if (dbus_message_iter_get_arg_type(&it) == DBUS_TYPE_ARRAY) extract_session(&it);
		printf("  session_handle=%s\n", g_session);
		printf("--\n");
	}
	if (strstr(iface, "GlobalShortcuts"))
	{
		printf("SIG iface=%s member=%s path=%s\n", iface, member?member:"?", dbus_message_get_path(m));
		DBusMessageIter it; dbus_message_iter_init(m,&it); print_iter(&it,0); printf("--\n");
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static void dict_two(DBusMessageIter *out, const char *k1,const char *v1, const char *k2,const char *v2)
{
	DBusMessageIter a,e,var;
	dbus_message_iter_open_container(out,DBUS_TYPE_ARRAY,"{sv}",&a);
	dbus_message_iter_open_container(&a,DBUS_TYPE_DICT_ENTRY,NULL,&e);
	dbus_message_iter_append_basic(&e,DBUS_TYPE_STRING,&k1);
	dbus_message_iter_open_container(&e,DBUS_TYPE_VARIANT,"s",&var); dbus_message_iter_append_basic(&var,DBUS_TYPE_STRING,&v1); dbus_message_iter_close_container(&e,&var);
	dbus_message_iter_close_container(&a,&e);
	dbus_message_iter_open_container(&a,DBUS_TYPE_DICT_ENTRY,NULL,&e);
	dbus_message_iter_append_basic(&e,DBUS_TYPE_STRING,&k2);
	dbus_message_iter_open_container(&e,DBUS_TYPE_VARIANT,"s",&var); dbus_message_iter_append_basic(&var,DBUS_TYPE_STRING,&v2); dbus_message_iter_close_container(&e,&var);
	dbus_message_iter_close_container(&a,&e);
	dbus_message_iter_close_container(out,&a);
}
static void dict_three(DBusMessageIter *out, const char *k1,const char *v1, const char *k2,const char *v2, const char *k3,const char *v3)
{
	DBusMessageIter a,e,var;
	dbus_message_iter_open_container(out,DBUS_TYPE_ARRAY,"{sv}",&a);
	dbus_message_iter_open_container(&a,DBUS_TYPE_DICT_ENTRY,NULL,&e);
	dbus_message_iter_append_basic(&e,DBUS_TYPE_STRING,&k1);
	dbus_message_iter_open_container(&e,DBUS_TYPE_VARIANT,"s",&var); dbus_message_iter_append_basic(&var,DBUS_TYPE_STRING,&v1); dbus_message_iter_close_container(&e,&var);
	dbus_message_iter_close_container(&a,&e);
	dbus_message_iter_open_container(&a,DBUS_TYPE_DICT_ENTRY,NULL,&e);
	dbus_message_iter_append_basic(&e,DBUS_TYPE_STRING,&k2);
	dbus_message_iter_open_container(&e,DBUS_TYPE_VARIANT,"s",&var); dbus_message_iter_append_basic(&var,DBUS_TYPE_STRING,&v2); dbus_message_iter_close_container(&e,&var);
	dbus_message_iter_close_container(&a,&e);
	dbus_message_iter_open_container(&a,DBUS_TYPE_DICT_ENTRY,NULL,&e);
	dbus_message_iter_append_basic(&e,DBUS_TYPE_STRING,&k3);
	dbus_message_iter_open_container(&e,DBUS_TYPE_VARIANT,"s",&var); dbus_message_iter_append_basic(&var,DBUS_TYPE_STRING,&v3); dbus_message_iter_close_container(&e,&var);
	dbus_message_iter_close_container(&a,&e);
	dbus_message_iter_close_container(out,&a);
}
static void dict_empty(DBusMessageIter *out){ DBusMessageIter a; dbus_message_iter_open_container(out,DBUS_TYPE_ARRAY,"{sv}",&a); dbus_message_iter_close_container(out,&a); }

static void shortcut(DBusMessageIter *aArr, const char *id, const char *desc, const char *key)
{
	DBusMessageIter st,dic,de,var,ta,tst,o;
	dbus_message_iter_open_container(aArr,DBUS_TYPE_STRUCT,NULL,&st);
	dbus_message_iter_append_basic(&st,DBUS_TYPE_STRING,&id);
	dbus_message_iter_open_container(&st,DBUS_TYPE_ARRAY,"{sv}",&dic);
	const char *kd="description";
	dbus_message_iter_open_container(&dic,DBUS_TYPE_DICT_ENTRY,NULL,&de);
	dbus_message_iter_append_basic(&de,DBUS_TYPE_STRING,&kd);
	dbus_message_iter_open_container(&de,DBUS_TYPE_VARIANT,"s",&var); dbus_message_iter_append_basic(&var,DBUS_TYPE_STRING,&desc); dbus_message_iter_close_container(&de,&var);
	dbus_message_iter_close_container(&dic,&de);
	const char *kt="triggers";
	dbus_message_iter_open_container(&dic,DBUS_TYPE_DICT_ENTRY,NULL,&de);
	dbus_message_iter_append_basic(&de,DBUS_TYPE_STRING,&kt);
	dbus_message_iter_open_container(&de,DBUS_TYPE_VARIANT,"a(ssa{sv})",&var);
	dbus_message_iter_open_container(&var,DBUS_TYPE_ARRAY,"(ssa{sv})",&ta);
	dbus_message_iter_open_container(&ta,DBUS_TYPE_STRUCT,NULL,&tst);
	const char *ty="keyboard";
	dbus_message_iter_append_basic(&tst,DBUS_TYPE_STRING,&ty);
	dbus_message_iter_append_basic(&tst,DBUS_TYPE_STRING,&key);
	dbus_message_iter_open_container(&tst,DBUS_TYPE_ARRAY,"{sv}",&o); dbus_message_iter_close_container(&tst,&o);
	dbus_message_iter_close_container(&ta,&tst);
	dbus_message_iter_close_container(&var,&ta);
	dbus_message_iter_close_container(&de,&var);
	dbus_message_iter_close_container(&dic,&de);
	dbus_message_iter_close_container(&st,&dic);
	dbus_message_iter_close_container(aArr,&st);
}

int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc>1) g_key=argv[1];
	DBusError err; dbus_error_init(&err);
	DBusConnection *conn=dbus_bus_get(DBUS_BUS_SESSION,&err);
	if(!conn){fprintf(stderr,"bus: %s\n",err.message);return 1;}
	dbus_connection_set_exit_on_disconnect(conn,FALSE);
	dbus_connection_add_filter(conn,filter,NULL,NULL);

	DBusMessage *msg=dbus_message_new_method_call("org.freedesktop.portal.Desktop","/org/freedesktop/portal/desktop","org.freedesktop.portal.GlobalShortcuts","CreateSession");
	DBusMessageIter it; dbus_message_iter_init_append(msg,&it);
	dict_three(&it,"session_handle_token","ahk_s1","handle_token","ahk_h1","app_id","org.autohotkey.linux");
	DBusMessage *rep=dbus_connection_send_with_reply_and_block(conn,msg,15000,&err);
	dbus_message_unref(msg);
	if(!rep){fprintf(stderr,"CreateSession failed: %s\n",err.message);return 1;}
	DBusMessageIter rit; dbus_message_iter_init(rep,&rit);
	const char *reqraw=0; dbus_message_iter_get_basic(&rit,&reqraw);
	char *req = reqraw ? strdup(reqraw) : NULL;
	printf("REQUEST=%s\n", req ? req : "(null)");
	dbus_message_unref(rep);
	if(!req) return 2;

	/* Match Request.Response for the request path. */
	char rule[512];
	snprintf(rule,sizeof(rule),"type='signal',interface='org.freedesktop.portal.Request',path='%s'", req);
	dbus_bus_add_match(conn,rule,&err);
	if(dbus_error_is_set(&err)){fprintf(stderr,"m: %s\n",err.message);dbus_error_free(&err);}
	/* also GlobalShortcuts on the session once known */
	free(req);

	/* Wait up to 10s for the Response with the real session handle. */
	printf("WAIT_SESSION...\n");
	int attempts=0;
	while (g_session[0]==0 && attempts<100) { dbus_connection_read_write_dispatch(conn,100); usleep(100000); attempts++; }
	/* If the portal did not echo a session_handle (version/impl quirk), build it
	 * from the sender unique name + session_handle_token, the documented path:
	 * /org/freedesktop/portal/desktop/session/<sender-without-:_->/<token>       */
	if (g_session[0]==0)
	{
		const char *uname = dbus_bus_get_unique_name(conn); /* e.g. ":1.226" */
		char sender[128] = "1";
		if (uname)
		{
			snprintf(sender, sizeof(sender), "%s", uname + 1); /* strip ':' */
			for (char *p = sender; *p; ++p) if (*p=='.') *p='_';
		}
		snprintf(g_session, sizeof(g_session),
			"/org/freedesktop/portal/desktop/session/%s/ahk_s1", sender);
	}
	printf("USING_SESSION=%s (resp_code=%d)\n", g_session, g_response_code);

	/* BindShortcuts(session, shortcuts, "/", {}) */
	msg=dbus_message_new_method_call("org.freedesktop.portal.Desktop","/org/freedesktop/portal/desktop","org.freedesktop.portal.GlobalShortcuts","BindShortcuts");
	dbus_message_iter_init_append(msg,&it);
	/* session must be an object path; session_handle historically came back as
	 * a string, so convert s -> o (it is a valid object path). */
	const char *sp = g_session;
	dbus_message_iter_append_basic(&it,DBUS_TYPE_OBJECT_PATH,&sp);
	dbus_message_iter_open_container(&it,DBUS_TYPE_ARRAY,"(sa{sv})",&rit);
	shortcut(&rit,"hk_id1","Probe 1",g_key);
	dbus_message_iter_close_container(&it,&rit);
	const char *pw="/"; dbus_message_iter_append_basic(&it,DBUS_TYPE_STRING,&pw);
	dict_empty(&it);
	rep=dbus_connection_send_with_reply_and_block(conn,msg,20000,&err);
	dbus_message_unref(msg);
	if(!rep){fprintf(stderr,"BindShortcuts failed: %s\n",err.message);return 4;}
	dbus_message_iter_init(rep,&rit);
	if(dbus_message_iter_get_arg_type(&rit)==DBUS_TYPE_OBJECT_PATH){ const char *qp=0; dbus_message_iter_get_basic(&rit,&qp); printf("BIND_REQUEST=%s\n",qp); }
	else { printf("Bind reply:"); print_iter(&rit,1); }
	dbus_message_unref(rep);

	printf("WATCHING... (press bound key physically to see Activated)\n");
	for(int i=0;i<120;i++){ dbus_connection_read_write_dispatch(conn,100); usleep(100000); }
	printf("DONE\n");
	return 0;
}
