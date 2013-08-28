/* vim: textwidth=79:
 *
 * Linux kmod alias lookup for PostgreSQL: Look up Linux kernel module names by
 * module alias string.
 *
 * o Compile with 'make'. See the included Makefile
 *
 * o Copy kmod_alias.so to `pg_config --pkglibdir`
 *
 * o Create the function:
 *
 *   CREATE FUNCTION kmod_alias_lookup(text) RETURNS SETOF text
 *     AS 'kmod_alias', 'kmod_alias_lookup'
 *     LANGUAGE C
 *     IMMUTABLE
 *     STRICT;
 *
 * o Try a query:
 *
 *   SELECT kmod_alias_lookup('pci:v00008086d00002653sv*sd*bc01sc01i*');
 *    kmod_alias_lookup
 *   -------------------
 *    ahci
 *    ata_piix
 *    ata_generic
 *    ata_generic
 *   (4 rows)
 */

#include <string.h>
#include <libkmod.h>
#include <limits.h>
#include "postgres.h"
#include "funcapi.h"
#include "utils/builtins.h"

#ifdef PG_MODULE_MAGIC
PG_MODULE_MAGIC;
#endif

/* Somewhat arbitrary maximum number of modules returned for an alias lookup.
 * The highest observed number of modules per alias is 9 for the
 * pci:v*d*sv*sd*bc0Dsc10i10* alias.
 */
#define SRF_MAXCALLS 32

/* A pointer to this structure is kept across calls */
struct kmod_alias_ctx
{
	struct kmod_ctx *kctx;
	struct kmod_list *list;
	struct kmod_list *itr;
};

Datum kmod_alias_lookup(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(kmod_alias_lookup);
Datum kmod_alias_lookup(PG_FUNCTION_ARGS)
{
	int err;
	struct kmod_alias_ctx *kactx;
	FuncCallContext *funcctx;

	if(SRF_IS_FIRSTCALL())
	{
		MemoryContext oldmemctx;

		/* Function context for cross-call persistence */
		funcctx = SRF_FIRSTCALL_INIT();

		/* Switch to multicall context, so that allocated memory
		 * survives across calls. Save the old context so that it can
		 * be restored after initialization.
		 */
		oldmemctx = MemoryContextSwitchTo(
				funcctx->multi_call_memory_ctx);

		/* Allocate user data (in the multi-call memory context) */
		kactx = (struct kmod_alias_ctx *)palloc(sizeof(struct
					kmod_alias_ctx));

		/* Initialize kmod */
		kactx->kctx = kmod_new(NULL, NULL);
		if(kactx->kctx == NULL)
			ereport(ERROR, (errmsg("kmod_new() failed")));

		/* Look up module alias */
		kactx->list = NULL;
		err = kmod_module_new_from_lookup(kactx->kctx,
				text_to_cstring(PG_GETARG_TEXT_P(0)),
				&kactx->list);
		if(err < 0)
			ereport(ERROR, (errmsg("kmod_module_new_from_lookup() "
					"failed: %s", strerror(err))));

		/* This is basically a manual expansion of the
		 * kmod_list_foreach macro so that we can iterate through it
		 * across calls.
		 */
		kactx->itr = ((kactx->list) == NULL) ? NULL : (kactx->list);

		/* Avoid run-away loops */
		funcctx->max_calls = SRF_MAXCALLS;

		/* Point user data to kactx */
		funcctx->user_fctx = kactx;

		/* Restore previous memory allocation context */
		MemoryContextSwitchTo(oldmemctx);
	}

	/* Obtain current function context pointer */
	funcctx = SRF_PERCALL_SETUP();

	/* Point kactx to the user data */
	kactx = (struct kmod_alias_ctx *)funcctx->user_fctx;

	if(kactx->itr != NULL && funcctx->call_cntr < funcctx->max_calls)
	{
		Datum result;
		struct kmod_module *mod;
		char *modname;

		/* Get module handle and name */
		mod = kmod_module_get_module(kactx->itr);
		modname = strdup(kmod_module_get_name(mod));
		kmod_module_unref(mod);

		/* Iterate to next list item */
		kactx->itr = kmod_list_next(kactx->list, kactx->itr);

		/* Return current result */
		result = CStringGetTextDatum(modname);
		free(modname);
		SRF_RETURN_NEXT(funcctx, result);
	}
	else
	{
		/* Unref kmod list and kmod itself */
		kmod_module_unref_list(kactx->list);
		kmod_unref(kactx->kctx);

		SRF_RETURN_DONE(funcctx);
	}
}
