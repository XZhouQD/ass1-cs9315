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

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>

#define TRUE 1
#define FALSE 0

PG_MODULE_MAGIC;

typedef int int4;

typedef struct Email
{
	int4 length;
	char text[1];
} EmailAddr;



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

int validEmail(char * str) {

	int length;
	char * temp;
	int domainLength;
	int localLength;

	//look for '@' to divide local & domain
	temp = strchr(str, '@');
	if (!temp)
		return FALSE;

	//Length check
	length = strlen(str);
	domainLength = length - ((temp-str)+1);
	localLength = temp-str;
	if (domainLength > 256)
		return FALSE;
	if (localLength > 256)
		return FALSE;

	char local[256] = {0};
	char domain[256] = {0};

	//copy into seperate string for checking
	strncpy(local, str, localLength);
	strncpy(domain, temp+1, domainLength);

	//regex checking
	if (!checkLocal(local))
		return FALSE;
	if (!checkDomain(domain))
		return FALSE;

	return TRUE;
}


/*****************************************************************************
 * Input/Output functions
 *****************************************************************************/

PG_FUNCTION_INFO_V1(email_in);

Datum
email_in(PG_FUNCTION_ARGS)
{
	char * str = PG_GETARG_CSTRING(0);


	EmailAddr * result;

	int length;
	length = strlen(str);

	if(!checkemal(str)
		printError(str);

	//to lower case
	char * lowerStr = (char *)palloc(length+1);
	strcpy(lowerStr, str);
	int i;
	for (i = 0; lowerStr[i]; i++)
		lowerStr[i] = tolower(lowerStr[i]);

	//store in struct
	result = (EmailAddr *) palloc(VARHDRSZ + length + 1); //header + string
	SET_VARSIZE(result, VARHDRSZ + length + 1);
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
	EmailAddr * e = (EmailAddr *) PG_GETARG_POINTER(0); //get the argument Email struct

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
	StringInfo buffer = (StringInfo) PG_GETARG_POINTER(0);
	int length = pg_getmsgint64(buffer);
	char * str = pg_getmsgstring(buffer);
	str[length-1] = '\0';

	EmailAddr * result = (EmailAddr *)palloc(length+VARHDRSZ);
	SET_VARSIZE(result, length+VARHDRSZ);
	snprintf(result->text, length, "%s", str);

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(email_send);

Datum
email_send(PG_FUNCTION_ARGS)
{
	EmailAddr *e = (EmailAddr *) PG_GETARG_POINTER(0);
	StringInfoData buffer;
	pg_begintypsend(&buffer);
	pg_sendint64(&buffer, VARSIZE(e)- VARHDRSZ);
	pg_sendstring(&buffer, e->text);
	PG_RETURN_BYTEA_P(pg_endtypsend(&buffer));
}


static int
email_cmp(Email * a, Email * b)
{
	int strLengthA = VARSIZE(a) - VARHDSIZE - 1;
	int strLengthB = VARSIZE(b) - VARHDSIZE - 1;
	char *domainApos = strchr(a->text, '@') + 1;
	char *domainBpos = strchr(b->text, '@') + 1;
	int localLengthA = domainA - a->text;
	int domainLengthA = strLengthA - localLengthA - 1;
	int localLengthB = domainB - b->text;
	int domainLengthB = strLengthB - localLengthB - 1;

	char localA[256] = {0};
	strncpy(localA, e->text, localLengthA);
	char localB[256] = {0};
	strncpy(localB, e->text, localLengthB);
	char domainA[256] = {0};
	strncpy(domainA, domainApos, domainLengthA);
	char domainB[256] = {0};
	strncpy(domainB, domainBpos, domainLengthB);

	int domain = strcmp(domainA,domainB);

	int local = strcmp(localA, localB);

	if(domain==0){
		if(local > 0)
			return 1;
		if(local == 0)
			return 0;
		if(local < 0)
			return -1;
	}
	if(domain > 0)
		return 2;
	if(domain < 0)
		return -2;
}
}
/*****************************************************************************
 * New Operators
 *
 * A practical Email datatype would provide much more than this, of course.

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

/*#define Mag(c)	((c)->x*(c)->x + (c)->y*(c)->y)


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
}*/


PG_FUNCTION_INFO_V1(email_eql);

Datum 
email_eq(PG_FUNCTION_ARGS)
{
	// todo
	//Create two email, use cmp function to compare them
	//if return value is 2, they are equal, else not equal
	EmailAddr *left = (EmailAddr *) PG_GETARG_POINTER(0); 
	EmailAddr *right = (EmailAddr *) PG_GETARG_POINTER(1);
	int isEqual;
	if(email_cmp(left,right)==0){
	    isEqual = TRUE;
	}else {
	    isEqual = FALSE;
	}
	PG_RETURN_BOOL(isEqual);
}

PG_FUNCTION_INFO_V1(email_gt);

Datum 
email_gt(PG_FUNCTION_ARGS)
{
	EmailAddr *left = (EmailAddr *) PG_GETARG_POINTER(0); 
	EmailAddr *right = (EmailAddr *) PG_GETARG_POINTER(1);
	int isGreater;
	if(email_cmp(left,right)>0){
	    isGreater = TRUE;
	}else {
	    isGreater = FALSE;
	}
	PG_RETURN_BOOL(isGreater);
}


PG_FUNCTION_INFO_V1(email_domain_eql);

Datum 
email_de(PG_FUNCTION_ARGS)
{
	// todo
	EmailAddr *left = (EmailAddr *) PG_GETARG_POINTER(0); 
	EmailAddr *right = (EmailAddr *) PG_GETARG_POINTER(1);
	int domainEqual;
	if(abs(email_cmp(left,right))<=1){
	    domainEqual= TRUE;
	}else {
	    domainEqual = FALSE;
	}
	PG_RETURN_BOOL(domainEqual);
}

PG_FUNCTION_INFO_V1(email_not_eql);

Datum 
email_ne(PG_FUNCTION_ARGS)
{
	//todo
	EmailAddr *left = (EmailAddr *) PG_GETARG_POINTER(0); 
	EmailAddr *right = (EmailAddr *) PG_GETARG_POINTER(1);
	int notEqual;
	if(email_cmp(left,right)!=0){
	    notEuqal = TRUE;
	}else {
	    notEuqal = FALSE;
	}
	PG_RETURN_BOOL(notEqual);
}

PG_FUNCTION_INFO_V1(email_ge);

Datum 
email_ge(PG_FUNCTION_ARGS)
{
	//todo
	EmailAddr *left = (EmailAddr *) PG_GETARG_POINTER(0); 
	EmailAddr *right = (EmailAddr *) PG_GETARG_POINTER(1);
	int isGreater_Equal;
	if(email_cmp(left,right)>=0){
	    isGreater_Equal = TRUE;
	}else {
	    isGreater_Equal = FALSE;
	}
	PG_RETURN_BOOL(isGreater_Equal);
}

PG_FUNCTION_INFO_V1(email_lt);

Datum 
email_lt(PG_FUNCTION_ARGS)
{
	// todo
	EmailAddr *left = (EmailAddr *) PG_GETARG_POINTER(0); 
	EmailAddr *right = (EmailAddr *) PG_GETARG_POINTER(1);
	int isLess;
	if(email_cmp(left,right)<0){
	    isLess = TRUE;
	}else {
	    isLess= FALSE;
	}
	PG_RETURN_BOOL(isLess);
}

PG_FUNCTION_INFO_V1(email_le);

Datum 
email_le(PG_FUNCTION_ARGS)
{
	// todo
	EmailAddr *left = (EmailAddr *) PG_GETARG_POINTER(0); 
	EmailAddr *right = (EmailAddr *) PG_GETARG_POINTER(1);
	int isLess_Equal;
	if(email_cmp(left,right)<=0){
	    isLess_Equal = TRUE;
	}else {
	    isLess_Equal = FALSE;
	}
	PG_RETURN_BOOL(isLess_Equal);
}

PG_FUNCTION_INFO_V1(email_domain_not_eql);

Datum 
email_dne(PG_FUNCTION_ARGS)
{
	// todo
	Email *left = (Email *) PG_GETARG_POINTER(0); 
	Email *right = (Email *) PG_GETARG_POINTER(1);
	int domain_notEqual;
	if(abs(email_cmp(left,right))>1){
	    domain_notEqual = TRUE;
	}else {
	    domain_notEqual = FALSE;
	}
	PG_RETURN_BOOL(domain_notEqual);
}

PG_FUNCTION_INFO_V1(email_abs_cmp);

Datum
email_abs_cmp(PG_FUNCTION_ARGS)
{
	EmailAddr * left = (EmailAddr *) PG_GETARGPOINTER(0);
	EmailAddr * right = (EmailAddr *) PG_GETARGPOINTER(1);

	int32 abs_cmp = email_cmp(left, right);
	if(abs_cmp > 0)
		abs_cmp = 1;
	else if (abs_cmp < 0)
		abs_cmp = -1;
	PG_RETURN_INT322(abs_cmp);
}


PG_FUNCTION_INFO_V1(email_check);

Datum
email_check(PG_FUNCTION_ARGS)
{
	EmailAddr * e = (EmailAddr *) PG_GETARG_POINTER(0);
	PG_RETURN_INT32(validEmail(e->text));
}

