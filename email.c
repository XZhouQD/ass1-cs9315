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
#include "access/hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

#define TRUE 1
#define FALSE 0

PG_MODULE_MAGIC;

typedef int int4;

typedef struct Email
{
	int4 length;
	char text[1];
} Email;

Datum email_in(PG_FUNCTION_ARGS);
Datum email_out(PG_FUNCTION_ARGS);
Datum email_recv(PG_FUNCTION_ARGS);
Datum email_send(PG_FUNCTION_ARGS);
Datum email_eq(PG_FUNCTION_ARGS);
Datum email_gt(PG_FUNCTION_ARGS);
Datum email_de(PG_FUNCTION_ARGS);
Datum email_ne(PG_FUNCTION_ARGS);
Datum email_ge(PG_FUNCTION_ARGS);
Datum email_lt(PG_FUNCTION_ARGS);
Datum email_le(PG_FUNCTION_ARGS);
Datum email_dne(PG_FUNCTION_ARGS);
Datum email_abs_cmp(PG_FUNCTION_ARGS);
Datum email_check(PG_FUNCTION_ARGS);
Datum email_hash(PG_FUNCTION_ARGS);


// Regex checking helper functions
int regexMatch(char * str, char * regexPattern) {
	regex_t regex;
	int match = FALSE;
	if(regcomp(&regex, regexPattern, REG_EXTENDED|REG_ICASE)) return -1;
	if(!regexec(&regex, str, 0, NULL, 0)) match = TRUE;
	regfree(&regex);
	return match;
}

// Match local part of the email address with regular expression
int checkLocal(char * local) {
	char * localRegex = "^([a-zA-Z]([-]*[a-zA-Z0-9])*[\\.])*[a-zA-Z]([-]*[a-zA-Z0-9])*$";
	if(!regexMatch(local, localRegex)) return FALSE;
	return TRUE;
}

// Match domain part of the email address with regular expression
int checkDomain(char * domain) {
	char * domainRegex = "^([a-zA-Z]([-]*[a-zA-Z0-9])*[\\.])+[a-zA-Z]([-]*[a-zA-Z0-9])*$";
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
	domainLength = length - (temp-str) - 1;
	localLength = temp-str;
	if (domainLength > 256)
		return FALSE;
	if (localLength > 256)
		return FALSE;

	char local[257] = {0};
	char domain[257] = {0};

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

	Email * result;

	int length;
	length = strlen(str);

	if(!validEmail(str))
		printError(str);

	//to lower case
	char * lowerStr = (char *)palloc(length+1);
	strcpy(lowerStr, str);
	int i = 0;
	for (i = 0; lowerStr[i]; i++)
		lowerStr[i] = tolower(lowerStr[i]);

	//store in struct
	result = (Email *) palloc(VARHDRSZ + length + 1); //header + string
	SET_VARSIZE(result, VARHDRSZ + length + 1);
	snprintf(result->text, length+1, "%s", lowerStr);

	PG_RETURN_POINTER(result);

}

PG_FUNCTION_INFO_V1(email_out);

Datum
email_out(PG_FUNCTION_ARGS)
{
	//get the argument Email struct
	Email * e = (Email *) PG_GETARG_POINTER(0);
	//length = VARSIZE - int4 + '\0'
	int resultLength = VARSIZE(e) - VARHDRSZ +1; 
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
	int length = pq_getmsgint64(buffer);
	char * str = pq_getmsgstring(buffer);
	str[length-1] = '\0';

	Email * result = (Email *)palloc(length+VARHDRSZ);
	SET_VARSIZE(result, length+VARHDRSZ);
	snprintf(result->text, length, "%s", str);

	PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(email_send);

Datum
email_send(PG_FUNCTION_ARGS)
{
	Email *e = (Email *) PG_GETARG_POINTER(0);
	StringInfoData buffer;
	pq_begintypsend(&buffer);
	// Send the length of email address
	pq_sendint64(&buffer, VARSIZE(e)- VARHDRSZ);
	pq_sendstring(&buffer, e->text);
	PG_RETURN_BYTEA_P(pq_endtypsend(&buffer));
}



static int
email_cmp(Email * a, Email * b)
{
	// Get str length
	int strLengthA = VARSIZE(a) - VARHDRSZ - 1;
	int strLengthB = VARSIZE(b) - VARHDRSZ - 1;

	// Get domain position
	char *domainApos = strchr(a->text, '@') + 1;
	char *domainBpos = strchr(b->text, '@') + 1;

	// Get local & domain length
	int localLengthA = domainApos - a->text;
	int domainLengthA = strLengthA - localLengthA - 1;
	int localLengthB = domainBpos - b->text;
	int domainLengthB = strLengthB - localLengthB - 1;

	// Get strs
	char localA[256] = {0};
	strncpy(localA, a->text, localLengthA);
	char localB[256] = {0};
	strncpy(localB, b->text, localLengthB);
	char domainA[256] = {0};
	strncpy(domainA, domainApos, domainLengthA);
	char domainB[256] = {0};
	strncpy(domainB, domainBpos, domainLengthB);

	int domain = strcmp(domainA,domainB);

	int local = strcmp(localA, localB);

	// Get return value
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

/*****************************************************************************
 * New Operators
 *
 * A practical Email datatype would provide much more than this, of course.

 *****************************************************************************
 * Operator class for defining B-tree index
 *
 * It's essential that the comparison operators and support function for a
 * B-tree index opclass always agree on the relative ordering of any two
 * data values.  Experience has shown that it's depressingly easy to write
 * unintentionally inconsistent functions.  One way to reduce the odds of
 * making a mistake is to make all the functions simple wrappers around
 * an internal three-way-comparison function, as we do here.
 *****************************************************************************/

PG_FUNCTION_INFO_V1(email_eq);

Datum 
email_eq(PG_FUNCTION_ARGS)
{

	// Create two email, use cmp function to compare them
	// Check equal or not
	Email *left = (Email *) PG_GETARG_POINTER(0); 
	Email *right = (Email *) PG_GETARG_POINTER(1);
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
	Email *left = (Email *) PG_GETARG_POINTER(0); 
	Email *right = (Email *) PG_GETARG_POINTER(1);
	int isGreater;
	if(email_cmp(left,right)>0){
	    isGreater = TRUE;
	}else {
	    isGreater = FALSE;
	}
	PG_RETURN_BOOL(isGreater);
}


PG_FUNCTION_INFO_V1(email_de);

Datum 
email_de(PG_FUNCTION_ARGS)
{

	Email *left = (Email *) PG_GETARG_POINTER(0); 
	Email *right = (Email *) PG_GETARG_POINTER(1);
	int domainEqual;
	// Absolute value check
	if(abs(email_cmp(left,right))<=1){
	    domainEqual= TRUE;
	}else {
	    domainEqual = FALSE;
	}
	PG_RETURN_BOOL(domainEqual);
}

PG_FUNCTION_INFO_V1(email_ne);

Datum 
email_ne(PG_FUNCTION_ARGS)
{

	Email *left = (Email *) PG_GETARG_POINTER(0); 
	Email *right = (Email *) PG_GETARG_POINTER(1);
	int notEqual;
	// Email comparation
	if(email_cmp(left,right)!=0){
	    notEqual = TRUE;
	}else {
	    notEqual = FALSE;
	}
	PG_RETURN_BOOL(notEqual);
}

PG_FUNCTION_INFO_V1(email_ge);

Datum 
email_ge(PG_FUNCTION_ARGS)
{

	Email *left = (Email *) PG_GETARG_POINTER(0); 
	Email *right = (Email *) PG_GETARG_POINTER(1);
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

	Email *left = (Email *) PG_GETARG_POINTER(0); 
	Email *right = (Email *) PG_GETARG_POINTER(1);
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

	Email *left = (Email *) PG_GETARG_POINTER(0); 
	Email *right = (Email *) PG_GETARG_POINTER(1);
	int isLess_Equal;
	if(email_cmp(left,right)<=0){
	    isLess_Equal = TRUE;
	}else {
	    isLess_Equal = FALSE;
	}
	PG_RETURN_BOOL(isLess_Equal);
}

PG_FUNCTION_INFO_V1(email_dne);

Datum 
email_dne(PG_FUNCTION_ARGS)
{

	Email *left = (Email *) PG_GETARG_POINTER(0); 
	Email *right = (Email *) PG_GETARG_POINTER(1);
	int domain_notEqual;
	// Absolute value check
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
	Email * left = (Email *) PG_GETARG_POINTER(0);
	Email * right = (Email *) PG_GETARG_POINTER(1);

	// Get abosolute values
	int32 abs_cmp = email_cmp(left, right);
	if(abs_cmp > 0)
		abs_cmp = 1;
	else if (abs_cmp < 0)
		abs_cmp = -1;
	PG_RETURN_INT32(abs_cmp);
}


PG_FUNCTION_INFO_V1(email_check);

Datum
email_check(PG_FUNCTION_ARGS)
{
	Email * e = (Email *) PG_GETARG_POINTER(0);
	PG_RETURN_INT32(validEmail(e->text));
}

PG_FUNCTION_INFO_V1(email_hash);

Datum
email_hash(PG_FUNCTION_ARGS)
{
	Email * e = (Email *) PG_GETARG_POINTER(0);
	int length = strlen(e->text);
	PG_RETURN_INT32(DatumGetUInt32(hash_any((const char *)e->text, length)));
}
