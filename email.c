/*
 * email.c
 *
 ******************************************************************************
  This file contains routines that can be bound to a Postgres backend and
  called by the backend in the process of processing queries.  The calling
  format for these routines is dictated by Postgres architecture.
******************************************************************************/

#include "postgres.h"

#include "fmgr.h"
#include "libpq/pqformat.h"		/* needed for send/recv functions */

#include <string.h>
#include <ctype.h>
#include <regex.h>


PG_MODULE_MAGIC;

typedef int int4;

typedef struct Email
{
	int4 length;
	char text[1];
} Email;



//Regex checking helper functions
int regexMatch(char * str, char * regexPattern) {
	regex_t regex;
	int match = FALSE;
	if(regcomp(&regex, regexPattern, REG_EXTENDED|REGICASE)) return -1;
	if(!regexec(&regex, str, 0, NULL, 0)) match = TRUE;
	regfree(&regex);
	return match;
}

int checkLocal(char * local) {
	char * localRegex = "^([a-zA-Z]([-]*[a-zA-Z0-9])*[\.])*[a-zA-Z]([-]*[a-zA-Z0-9])*$";
	if(!regexMatch(local, localRegex)) return FALSE;
	return TRUE;
}

int checkDomain(char * domain) {
	char * domainRegex = "^([a-zA-Z]([-]*[a-zA-Z0-9])*[\.])+[a-zA-Z]([-]*[a-zA-Z0-9])*$";
	if(!regexMatch(domain, domainRegex)) return FALSE;
	return TRUE;
}

void printError(char * str) {
	ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
}


/*****************************************************************************
 * Input/Output functions
 *****************************************************************************/

PG_FUNCTION_INFO_V1(email_in);

Datum
email_in(PG_FUNCTION_ARGS)
{
	char * str = PG_GETARG_CSTRING(0);

	char local[256] = {0};
	char domain[256] = {0};

	Email * result;

	char * temp;
	int length;
	int domainLength;
	int localLength;

	//look for '@' to divide local & domain
	temp = strchr(str, '@');
	if (!temp)
		printError(str);

	//Length check
	length = strlen(str);
	domainLength = length - ((temp-str)+1);
	localLength = temp-str;
	if (domainLength > 256)
		printError(str);
	if (localLength > 256)
		printError(str);

	//copy into seperate string for checking
	strncpy(local, str, localLength);
	strncpy(domain, temp+1, domainLength);

	//regex checking
	if (!checkLocal(local))
		printError(str);
	if (!checkDomain(domain))
		printError(str);

	//to lower case
	char * lowerStr = (char *)palloc(length+1);
	strcpy(lowerStr, str);
	int i;
	for (i = 0; lowerStr[i]; i++)
		lowerStr[i] = tolower(lowerStr[i]);

	//store in struct
	result = (Email *) palloc(VARHDRSZ + length); //2*int4 + string without '@'
	SET_VARSIZE(result, VARHDRSZ + length);
	snprintf(result->text, length+1, "%s", lowerStr);

	PG_RETURN_POINTER(result);

/*
// read in email address

	while(*(str+i) != '@' && *(str+i) != '\0') {
		i++;
	}
	if (*(str+i) == '@' && i > 0) {
		strncpy(local, str, i);
		i++;
		temp = str+i;
		i = 0;
	} else {
		ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
	}
	while(*(temp+i) != '@' && *(temp+i) != '\0') {
		i++;
	}
	if (*(temp+i) == '@' || i == 0) {
		ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
	} else {
		strncpy(domain, temp, i);
	}

//Syntex test of parts
	int wordLength = 0;
	int wordCount = 0;
	char tempEnd = 0;
	i = 0;
	while(local[i] != '\0') {
		if (local[i] == '.') {
			if(wordLength == 0) {
				ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
			}
			if(!isalnum(tempEnd)) {
				ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
			}
			wordLength = 0;
			tempEnd = 0;
		}
		if (wordLength == 0) {
			if(!isalpha(local[i])) {
				ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
			}
			wordCount++;
		}
		if(!(isalnum(local[i])||(local[i] == '-')) {
				ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
		}
		wordLength++;
		tempEnd = local[i];
	}

	wordLength = 0;
	wordCount = 0;
	tempEnd = 0;
	while(domain[i] != '\0') {
		if (domain[i] == '.') {
			if(wordLength == 0) {
				ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
			}
			if(!isalnum(tempEnd)) {
				ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
			}
			wordLength = 0;
			tempEnd = 0;
		}
		if (wordLength == 0) {
			if(!isalpha(domain[i])) {
				ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
			}
			wordCount++;
		}
		if(!(isalnum(domain[i])||(domain[i] == '-')) {
				ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
		}
		wordLength++;
		tempEnd = domain[i];
	}
	if (wordCount < 2) {
		ereport(ERROR,(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION), errmsg("invalid input syntax for email: \"%s\"",str)));
	}

	i = 0;
	while(local[i] != '\0') {
		local[i] = tolower(local[i]);
		i++;
	}

	i = 0;
	while(local[i] != '\0') {
		domain[i] = tolower(domain[i]);
		i++;
	}

	result = (Email *) palloc(sizeof(Email));
	result->local = strdup(local);
	result->domain = strdup(domain);
*/
}

PG_FUNCTION_INFO_V1(email_out);

Datum
email_out(PG_FUNCTION_ARGS)
{
	Email * e = (Email *) PG_GETARG_POINTER(0); //get the argument Email struct

	int resultLength = VARSIZE(e) - VARHDRSZ +1; //length = VARSIZE - int4*2 + '@' + '\0'
	char * result = palloc(resultLength);

	snprintf(result, resultLength, "%s", e->text);

	PG_RETURN_CSTRING(result);
}

/*****************************************************************************
 * Binary Input/Output functions
 *
 * These are optional.
 *****************************************************************************/

PG_FUNCTION_INFO_V1(email_recv);

Datum
email_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);
	Complex    *result;

	result = (Complex *) palloc(sizeof(Complex));
	result->x = pq_getmsgfloat8(buf);
	result->y = pq_getmsgfloat8(buf);
	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(email_send);

Datum
email_send(PG_FUNCTION_ARGS)
{
	Complex    *complex = (Complex *) PG_GETARG_POINTER(0);
	StringInfoData buf;

	pq_begintypsend(&buf);
	pq_sendfloat8(&buf, complex->x);
	pq_sendfloat8(&buf, complex->y);
	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*****************************************************************************
 * New Operators
 *
 * A practical Email datatype would provide much more than this, of course.
 *****************************************************************************/

PG_FUNCTION_INFO_V1(email_add);

Datum
email_add(PG_FUNCTION_ARGS)
{
	Complex    *a = (Complex *) PG_GETARG_POINTER(0);
	Complex    *b = (Complex *) PG_GETARG_POINTER(1);
	Complex    *result;

	result = (Complex *) palloc(sizeof(Complex));
	result->x = a->x + b->x;
	result->y = a->y + b->y;
	PG_RETURN_POINTER(result);
}


/*****************************************************************************
 * Operator class for defining B-tree index
 *
 * It's essential that the comparison operators and support function for a
 * B-tree index opclass always agree on the relative ordering of any two
 * data values.  Experience has shown that it's depressingly easy to write
 * unintentionally inconsistent functions.  One way to reduce the odds of
 * making a mistake is to make all the functions simple wrappers around
 * an internal three-way-comparison function, as we do here.
 *****************************************************************************/

#define Mag(c)	((c)->x*(c)->x + (c)->y*(c)->y)

static int
email_abs_cmp_internal(Email * a, Email * b)
{
	double		amag = Mag(a),
				bmag = Mag(b);

	if (amag < bmag)
		return -1;
	if (amag > bmag)
		return 1;
	return 0;
}


PG_FUNCTION_INFO_V1(email_abs_lt);

Datum
email_abs_lt(PG_FUNCTION_ARGS)
{
	Complex    *a = (Complex *) PG_GETARG_POINTER(0);
	Complex    *b = (Complex *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_abs_cmp_internal(a, b) < 0);
}

PG_FUNCTION_INFO_V1(email_abs_le);

Datum
email_abs_le(PG_FUNCTION_ARGS)
{
	Complex    *a = (Complex *) PG_GETARG_POINTER(0);
	Complex    *b = (Complex *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_abs_cmp_internal(a, b) <= 0);
}

PG_FUNCTION_INFO_V1(email_abs_eq);

Datum
email_abs_eq(PG_FUNCTION_ARGS)
{
	Complex    *a = (Complex *) PG_GETARG_POINTER(0);
	Complex    *b = (Complex *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_abs_cmp_internal(a, b) == 0);
}

PG_FUNCTION_INFO_V1(email_abs_ge);

Datum
email_abs_ge(PG_FUNCTION_ARGS)
{
	Complex    *a = (Complex *) PG_GETARG_POINTER(0);
	Complex    *b = (Complex *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_abs_cmp_internal(a, b) >= 0);
}

PG_FUNCTION_INFO_V1(email_abs_gt);

Datum
email_abs_gt(PG_FUNCTION_ARGS)
{
	Complex    *a = (Complex *) PG_GETARG_POINTER(0);
	Complex    *b = (Complex *) PG_GETARG_POINTER(1);

	PG_RETURN_BOOL(email_abs_cmp_internal(a, b) > 0);
}

PG_FUNCTION_INFO_V1(email_abs_cmp);

Datum
email_abs_cmp(PG_FUNCTION_ARGS)
{
	Complex    *a = (Complex *) PG_GETARG_POINTER(0);
	Complex    *b = (Complex *) PG_GETARG_POINTER(1);

	PG_RETURN_INT32(email_abs_cmp_internal(a, b));
}
