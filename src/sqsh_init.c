/*
 * sqsh_init.c - Initialize/cleanup global variable
 *
 * Copyright (C) 1995, 1996 by Scott C. Gray
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, write to the Free Software
 * Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * You may contact the author :
 *   e-mail:  gray@voicenet.com
 *            grays@xtend-tech.com
 *            gray@xenotropic.com
 */
#include <stdio.h>
#include "sqsh_config.h"
#include "sqsh_error.h"
#include "sqsh_global.h"
#include "sqsh_expand.h"
#include "sqsh_cmd.h"
#include "sqsh_env.h"
#include "sqsh_readline.h"
#include "sqsh_stdin.h"
#include "sqsh_init.h"
/*
 * The following defines the tables which are used to inialize the
 * variable global variables.
 */
#define SQSH_INIT
#include "cmd.h"
#include "var.h"
#include "alias.h"
#undef SQSH_INIT

/*-- Current Version --*/
#if !defined(lint) && !defined(__LINT__)
static char RCS_Id[] = "$Id: sqsh_init.c,v 1.1.1.1 2001/10/23 20:31:06 gray Exp $" ;
USE(RCS_Id)
#endif /* !defined(lint) */

/*
 * sqsh_init():
 *
 * Initializes all global variables for the first time. This function
 * should be pretty much the first thing called. True is returned
 * upon success, False is returned otherwise.
 *
 * Note: Careful attention should be payed to sqsh_fork.c and how it
 * interacts with these variables.
 */
int sqsh_init()
{
	int        i ;
	char      *histsize ;
	varbuf_t  *expand_buf ;

	/*
	 * g_connection: This variable is initiazed to NULL.  It is the responsibility 
	 *           of a user function to actually perform the connection to
	 *           set it.  This allows sqsh to be started without actually
	 *           connecting to the database.
	 */
	g_context    = NULL ;
	g_connection = NULL ;

	/*
	 * g_sqlbuf: This variable contains the working SQL buffer being typed
	 *           into by the user.  It is initialized to an empty buffer.
	 */
	if( (g_sqlbuf = varbuf_create( 1024 )) == NULL ) {
		sqsh_set_error( sqsh_get_error(), "varbuf_create: %s", sqsh_get_errstr());
		varbuf_destroy( g_sqlbuf ) ;
		return False ;
	}

	/*
	 * g_cmdset: The set of commands that may be executed by a user or
	 *           internally.
	 */
	if( (g_cmdset = cmdset_create()) == NULL ) {
		sqsh_set_error( sqsh_get_error(), "cmdset_create: %s", sqsh_get_errstr());
		return False ;
	}

	/*
	 * g_funcset: The set of commands that may be executed by a user or
	 *           internally.
	 */
	if ((g_funcset = funcset_create()) == NULL)
	{
		sqsh_set_error( sqsh_get_error(), 
			"cmdset_create: %s", sqsh_get_errstr());
		return False;
	}

	/*
	 * Now, populate the g_cmdset with the set of commands defined in
	 * cmd.h.
	 */
	for( i = 0; i < (sizeof(sg_cmd_entry) / sizeof(cmd_entry_t)); i++ ) {

		if( cmdset_add( g_cmdset, 
							 sg_cmd_entry[i].ce_name,
							 sg_cmd_entry[i].ce_func ) == False ) {
			sqsh_set_error( sqsh_get_error(), "cmdset_add: %s",
								 sqsh_get_errstr() ) ;
			return False ;
		}

	}

	/*
	 * Initialize our table of aliases
	 */
	if( (g_alias = alias_create()) == NULL ) {
		sqsh_set_error(sqsh_get_error(), "alias_create: %s", sqsh_get_errstr()) ;
		return False ;
	}

	for( i = 0; i < (sizeof(sg_alias_entry) / sizeof(alias_entry_t)); i++ ) {
		if( alias_add( g_alias, 
		               sg_alias_entry[i].ae_name,
		               sg_alias_entry[i].ae_body ) == False ) {
			sqsh_set_error( sqsh_get_error(), "alias_add: %s: %s",
			                sg_alias_entry[i].ae_name,
			                sqsh_get_errstr() ) ;
			return False ;
		}
	}

	/*
	 * g_buf:    Where named buffers are stored.
	 */
	if( (g_buf = env_create( 47 )) == NULL ) {
		sqsh_set_error( sqsh_get_error(), "env_create: %s", sqsh_get_errstr() ) ;
		return False ;
	}

	/*
	 * g_internal_env: This is the internal environment information that
	 *           is not settable by a user, but is queryable.
	 *
	 */
	if( (g_internal_env = env_create(47)) == NULL ) {
		sqsh_set_error( sqsh_get_error(), "env_create: %s", sqsh_get_errstr() ) ;
		return False ;
	}

	/*
	 * g_env:    This is the environment for the process.  Here we simply
	 *           create the environment and initialize it with the default
	 *           values, these may then be overridden by the users .sqshrc
	 */
	if( (g_env = env_create( 47 )) == NULL ) {
		sqsh_set_error( sqsh_get_error(), "env_create: %s", sqsh_get_errstr() ) ;
		return False ;
	}

	/*
	 * The following buffer is used to expand the values of the variables
	 * prior to setting them within the environment.  It is destroyed when
	 * we are done with this process.
	 */
 	if( (expand_buf = varbuf_create( 64 )) == NULL ) {
		sqsh_set_error(sqsh_get_error(), "varbuf_create: %s", sqsh_get_errstr());
		return False ;
 	}

	/*
	 * Now, using the array defined in var.h, populate the global
	 * environment.
	 */
	for( i = 0; i < (sizeof(sg_var_entry) / sizeof(var_entry_t)); i++ ) {

		if( sg_var_entry[i].ve_value != NULL ) {
			/*
			 * Expand the value of the variable prior to sticking it in the
			 * environment.  This, more-or-less, emulates the behaviour as
			 * if you had typed it in from the command line.
			 */
			if( sqsh_expand( sg_var_entry[i].ve_value, expand_buf, 
			                 EXP_STRIPESC ) == False ){
				sqsh_set_error( sqsh_get_error(), "%s: %s", 
									 sg_var_entry[i].ve_value, sqsh_get_errstr());
				return False ;
			}

			if( env_set_valid(
					g_env,                     /* Global environment */
					sg_var_entry[i].ve_name,   /* Name of variable */
					varbuf_getstr(expand_buf), /* Default value */
					sg_var_entry[i].ve_set,    /* Settor function */
					sg_var_entry[i].ve_get ) == False ) {
				sqsh_set_error( sqsh_get_error(), "env_set_valid: %s", 
									 sqsh_get_errstr() ) ;
				return False ;
			}
		} else {  /* NULL value */
			if( env_set_valid(
					g_env,                     /* Global environment */
					sg_var_entry[i].ve_name,   /* Name of variable */
					NULL,                      /* NULL value */
					sg_var_entry[i].ve_set,    /* Settor function */
					sg_var_entry[i].ve_get ) == False ) {
				sqsh_set_error( sqsh_get_error(), "env_set_valid: %s", 
									 sqsh_get_errstr() ) ;
				return False ;
			}
		}
	}

	/*-- Don't need it any more --*/
	varbuf_destroy( expand_buf ) ;

	/*
	 * Allocate our global set of sub-processes.
	 */
	if( (g_jobset = jobset_create( 47 )) == NULL ) {
		sqsh_set_error( sqsh_get_error(), "jobset_create: %s", 
							 sqsh_get_errstr() ) ;
	 	return False ;
	}

	/*
	 * Since the global environment set has been created and initialized
	 * we can retrieve any variables that may affect the size of other
	 * data structures...such as the history.
	 */
	env_get( g_env, "histsize", &histsize ) ;
	if( histsize == NULL || (i = atoi( histsize )) == 0 )
		i = 10 ;

	if( (g_history = history_create( i )) == NULL ) {
		sqsh_set_error( sqsh_get_error(), "history_create: %s", 
							 sqsh_get_errstr() ) ;
	 	return False ;
	}

	sqsh_set_error( SQSH_E_NONE, NULL ) ;
	return True ;
}

void sqsh_exit( exit_status )
	int  exit_status ;
{
	char      *history;
	char      *histsave;
	CS_INT     con_status;
	varbuf_t  *exp_buf;

	/*
	 * If the history has been created, and it contains items, and $history
	 * has been defined, then we write the history out to a file.
	 */
	if (g_history != NULL)
	{
		if (g_env != NULL)
		{
			env_get( g_env, "history", &history );
			env_get( g_env, "histsave", &histsave );
		}
		else
		{
			history = NULL;
			histsave = NULL;
		}

		if (sqsh_stdin_isatty() &&
		    (histsave == NULL || *histsave == '1')       &&
		    history != NULL && history_get_nitems( g_history ) > 0)
		{
			exp_buf = varbuf_create( 512 );

			if (exp_buf == NULL)
			{
				fprintf( stderr, "sqsh_exit: %s\n", sqsh_get_errstr() );
			}
			else
			{
				if (sqsh_expand( history, exp_buf, 0 ) == False)
				{
					fprintf( stderr, "sqsh_exit: Error expanding $history: %s\n",
						sqsh_get_errstr() );
				}
				else
				{
					history_save( g_history, varbuf_getstr(exp_buf) );
				}

				varbuf_destroy( exp_buf );
			}
		}

		history_destroy( g_history );
	}

	/*
	 * Clean up readline and write out the history buffer.
	 */
	sqsh_readline_exit();

	if( g_connection != NULL )
	{
		DBG(sqsh_debug(DEBUG_ERROR, "sqsh_exit: Getting connection status\n");)
		if (ct_con_props( g_connection,           /* Connection */
		                  CS_GET,                 /* Action */
		                  CS_CON_STATUS,          /* Property */
		                  (CS_VOID*)&con_status,  /* Buffer */
		                  CS_UNUSED,              /* Buffer Length */
		                  (CS_INT*)NULL ) != CS_SUCCEED)
		{
			DBG(sqsh_debug(DEBUG_ERROR, "sqsh_exit: Failed.\n");)
			con_status = CS_CONSTAT_CONNECTED;
		}

		/*-- If connected, disconnect --*/
		if (con_status == CS_CONSTAT_CONNECTED)
		{  
			DBG(sqsh_debug(DEBUG_ERROR, "sqsh_exit: Closing connection\n");)
			ct_close( g_connection, CS_FORCE_CLOSE );
		}

		ct_con_drop( g_connection );
	}

	if (g_context != NULL)
	{
		ct_exit( g_context, CS_FORCE_EXIT );
		cs_ctx_drop( g_context );
	}

	if( g_buf != NULL )
		env_destroy( g_buf ) ;

	if( g_env != NULL )
		env_destroy( g_env ) ;

	if( g_sqlbuf != NULL )
		varbuf_destroy( g_sqlbuf ) ;

	if( g_cmdset != NULL )
		cmdset_destroy( g_cmdset ) ;

	if( g_funcset != NULL )
		funcset_destroy( g_funcset ) ;

	if( g_jobset != NULL )
		jobset_destroy( g_jobset ) ;


	exit(exit_status) ;
}

