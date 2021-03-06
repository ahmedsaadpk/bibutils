/*
 * wordout.c
 * 
 * (Word 2007 format)
 *
 * Copyright (c) Chris Putnam 2007-2014
 *
 * Source code released under the GPL version 2
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "newstr.h"
#include "fields.h"
#include "utf8.h"
#include "wordout.h"

void
wordout_initparams( param *p, const char *progname )
{
	p->writeformat      = BIBL_WORD2007OUT;
	p->format_opts      = 0;
	p->charsetout       = BIBL_CHARSET_UNICODE;
	p->charsetout_src   = BIBL_SRC_DEFAULT;
	p->latexout         = 0;
	p->utf8out          = BIBL_CHARSET_UTF8_DEFAULT;
	p->utf8bom          = BIBL_CHARSET_BOM_DEFAULT;
	if ( !p->utf8out )
		p->xmlout   = BIBL_XMLOUT_ENTITIES;
	else
		p->xmlout   = BIBL_XMLOUT_TRUE;
	p->nosplittitle     = 0;
	p->verbose          = 0;
	p->addcount         = 0;
	p->singlerefperfile = 0;

	p->headerf = wordout_writeheader;
	p->footerf = wordout_writefooter;
	p->writef  = wordout_write;
}

typedef struct convert {
	char oldtag[25];
	char newtag[25];
	int  code;
} convert;

/*
At the moment 17 unique types of sources are defined:

{code}
	Art
	ArticleInAPeriodical
	Book
	BookSection
	Case
	Conference
	DocumentFromInternetSite
	ElectronicSource
	Film
	InternetSite
	Interview
	JournalArticle
	Report
	Misc
	Patent
	Performance
	Proceedings
	SoundRecording
{code}

*/

enum {
	TYPE_UNKNOWN = 0,
	TYPE_ART,
	TYPE_ARTICLEINAPERIODICAL,
	TYPE_BOOK,
	TYPE_BOOKSECTION,
	TYPE_CASE,
	TYPE_CONFERENCE,
	TYPE_DOCUMENTFROMINTERNETSITE,
	TYPE_ELECTRONICSOURCE,
	TYPE_FILM,
	TYPE_INTERNETSITE,
	TYPE_INTERVIEW,
	TYPE_JOURNALARTICLE,
	TYPE_MISC,
	TYPE_PATENT,
	TYPE_PERFORMANCE,
	TYPE_PROCEEDINGS,
	TYPE_REPORT,
	TYPE_SOUNDRECORDING,

	TYPE_THESIS,
	TYPE_MASTERSTHESIS,
	TYPE_PHDTHESIS,
};

/*
 * fixed output
 */
static void
output_fixed( FILE *outptr, char *tag, char *data, int level )
{
	int i;
	for ( i=0; i<level; ++i ) fprintf( outptr, " " );
	fprintf( outptr, "<%s>%s</%s>\n", tag, data, tag );
}

/* detail output
 *
 */
static void
output_item( fields *info, FILE *outptr, char *tag, int item, int level )
{
	int i;
	if ( item==-1 ) return;
	for ( i=0; i<level; ++i ) fprintf( outptr, " " );
	fprintf( outptr, "<%s>%s</%s>\n", tag, info->data[item].data, tag );
	fields_setused( info, item );
}

static void
output_itemv( FILE *outptr, char *tag, char *item, int level )
{
	int i;
	for ( i=0; i<level; ++i ) fprintf( outptr, " " );
	fprintf( outptr, "<%s>%s</%s>\n", tag, item, tag );
}

/* range output
 *
 * <TAG>start-end</TAG>
 *
 */
static void
output_range( FILE *outptr, char *tag, char *start, char *end, int level )
{
	int i;
	if ( start==NULL && end==NULL ) return;
	if ( start==NULL )
		output_itemv( outptr, tag, end, 0 );
	else if ( end==NULL )
		output_itemv( outptr, tag, start, 0 );
	else {
		for ( i=0; i<level; ++i )
			fprintf( outptr, " " );
		fprintf( outptr, "<%s>%s-%s</%s>\n", tag, start, end, tag );
	}
}

static void
output_list( fields *info, FILE *outptr, convert *c, int nc )
{
        int i, n;
        for ( i=0; i<nc; ++i ) {
                n = fields_find( info, c[i].oldtag, c[i].code );
                if ( n!=-1 ) output_item( info, outptr, c[i].newtag, n, 0 );
        }

}

typedef struct outtype {
	int value;
	char *out;
} outtype;

static
outtype genres[] = {
	{ TYPE_PATENT, "patent" },
	{ TYPE_REPORT, "report" },
	{ TYPE_CASE,   "legal case and case notes" },
	{ TYPE_ART,    "art original" },
	{ TYPE_ART,    "art reproduction" },
	{ TYPE_ART,    "comic strip" },
	{ TYPE_ART,    "diorama" },
	{ TYPE_ART,    "graphic" },
	{ TYPE_ART,    "model" },
	{ TYPE_ART,    "picture" },
	{ TYPE_ELECTRONICSOURCE, "electronic" },
	{ TYPE_FILM,   "videorecording" },
	{ TYPE_FILM,   "motion picture" },
	{ TYPE_SOUNDRECORDING, "sound" },
	{ TYPE_PERFORMANCE, "rehersal" },
	{ TYPE_INTERNETSITE, "web site" },
	{ TYPE_INTERVIEW, "interview" },
	{ TYPE_INTERVIEW, "communication" },
	{ TYPE_MISC, "misc" },
};
int ngenres = sizeof( genres ) / sizeof( genres[0] );

static int
get_type_from_genre( fields *info )
{
	int type = TYPE_UNKNOWN, i, j, level;
	char *genre;
	for ( i=0; i<info->n; ++i ) {
		if ( strcasecmp( info->tag[i].data, "GENRE" ) &&
			strcasecmp( info->tag[i].data, "NGENRE" ) ) continue;
		genre = info->data[i].data;
		for ( j=0; j<ngenres; ++j ) {
			if ( !strcasecmp( genres[j].out, genre ) )
				type = genres[j].value;
		}
		if ( type==TYPE_UNKNOWN ) {
			level = info->level[i];
			if ( !strcasecmp( genre, "academic journal" ) ) {
				type = TYPE_JOURNALARTICLE;
			}
			else if ( !strcasecmp( genre, "periodical" ) ) {
				if ( type == TYPE_UNKNOWN )
					type = TYPE_ARTICLEINAPERIODICAL;
			}
			else if ( !strcasecmp( genre, "book" ) ||
				!strcasecmp( genre, "collection" ) ) {
				if ( info->level[i]==0 ) type = TYPE_BOOK;
				else type = TYPE_BOOKSECTION;
			}
			else if ( !strcasecmp( genre, "conference publication" ) ) {
				if ( level==0 ) type=TYPE_CONFERENCE;
				type = TYPE_PROCEEDINGS;
			}
			else if ( !strcasecmp( genre, "thesis" ) ) {
	                        if ( type==TYPE_UNKNOWN ) type=TYPE_THESIS;
			}
			else if ( !strcasecmp( genre, "Ph.D. thesis" ) ) {
				type = TYPE_PHDTHESIS;
			}
			else if ( !strcasecmp( genre, "Masters thesis" ) ) {
				type = TYPE_MASTERSTHESIS;
			}
		}
	}
	return type;
}

static int
get_type_from_resource( fields *info )
{
	int type = TYPE_UNKNOWN, i;
	char *resource;
	for ( i=0; i<info->n; ++i ) {
		if ( strcasecmp( info->tag[i].data, "GENRE" )!=0 &&
			strcasecmp( info->tag[i].data, "NGENRE" )!=0 ) continue;
		resource = info->data[i].data;
		if ( !strcasecmp( resource, "moving image" ) )
			type = TYPE_FILM;
	}
	return type;
}

static int
get_type( fields *info )
{
	int type;
	type = get_type_from_genre( info );
	if ( type==TYPE_UNKNOWN )
		type = get_type_from_resource( info );
	return type;
}

static void
output_titleinfo( fields *info, FILE *outptr, char *tag, int level )
{
	newstr *mainttl = fields_findv( info, level, FIELDS_STRP, "TITLE" );
	newstr *subttl  = fields_findv( info, level, FIELDS_STRP, "SUBTITLE" );
	if ( mainttl || subttl ) {
		fprintf( outptr, "<%s>", tag );
		if ( mainttl ) fprintf( outptr, "%s", mainttl->data );
		if ( subttl ) {
			if ( mainttl ) {
				if ( mainttl->len > 0 &&
				     mainttl->data[mainttl->len-1]!='?' )
					fprintf( outptr, ": " );
				else fprintf( outptr, " " );
			}
			fprintf( outptr, "%s", subttl->data );
		}
		fprintf( outptr, "</%s>\n", tag );
	}
}

static void
output_title( fields *info, FILE *outptr, int level )
{
	char *ttl    = fields_findv( info, level, FIELDS_CHRP, "TITLE" );
	char *subttl = fields_findv( info, level, FIELDS_CHRP, "SUBTITLE" );
	char *shrttl = fields_findv( info, level, FIELDS_CHRP, "SHORTTITLE" );

	output_titleinfo( info, outptr, "b:Title", level );

	/* output shorttitle if it's different from normal title */
	if ( shrttl ) {
		if ( !ttl || ( strcmp( shrttl, ttl ) || subttl ) ) {
			fprintf( outptr,  " <b:ShortTitle>" );
			fprintf( outptr, "%s", shrttl );
			fprintf( outptr, "</b:ShortTitle>\n" );
		}
	}
}

static void
output_name_nomangle( FILE *outptr, char *p )
{
	fprintf( outptr, "<b:Person>" );
	fprintf( outptr, "<b:Last>%s</b:Last>", p );
	fprintf( outptr, "</b:Person>\n" );
}

static void
output_name( FILE *outptr, char *p )
{
	newstr family, part;
	int n=0, npart=0;

	newstr_init( &family );
	while ( *p && *p!='|' ) newstr_addchar( &family, *p++ );
	if ( *p=='|' ) p++;
	if ( family.len ) {
		fprintf( outptr, "<b:Person>" );
		fprintf( outptr, "<b:Last>%s</b:Last>",family.data );
		n++;
	}
	newstr_free( &family );

	newstr_init( &part );
	while ( *p ) {
		while ( *p && *p!='|' ) newstr_addchar( &part, *p++ );
		if ( part.len ) {
			if ( n==0 ) fprintf( outptr, "<b:Person>" );
			if ( npart==0 ) 
				fprintf( outptr, "<b:First>%s</b:First>",
					part.data );
			else fprintf( outptr, "<b:Middle>%s</b:Middle>",
					part.data );
			n++;
			npart++;
		}
		if ( *p=='|' ) {
			p++;
			newstr_empty( &part );
		}
	}
	if ( n ) fprintf( outptr, "</b:Person>\n" );

	newstr_free( &part );
}


#define NAME (1)
#define NAME_ASIS (2)
#define NAME_CORP (4)

static int
extract_name_and_info( newstr *outtag, newstr *intag )
{
	int code = NAME;
	newstr_newstrcpy( outtag, intag );
	if ( newstr_findreplace( outtag, ":ASIS", "" ) ) code = NAME_ASIS;
	if ( newstr_findreplace( outtag, ":CORP", "" ) ) code = NAME_CORP;
	return code;
}

static void
output_name_type( fields *info, FILE *outptr, int level, 
			char *map[], int nmap, char *tag )
{
	newstr ntag;
	int i, j, n=0, code, nfields;
	newstr_init( &ntag );
	nfields = fields_num( info );
	for ( j=0; j<nmap; ++j ) {
		for ( i=0; i<nfields; ++i ) {
			code = extract_name_and_info( &ntag, &(info->tag[i]) );
			if ( strcasecmp( ntag.data, map[j] ) ) continue;
			if ( n==0 )
				fprintf( outptr, "<%s><b:NameList>\n", tag );
			if ( code != NAME )
				output_name_nomangle( outptr, info->data[i].data );
			else 
				output_name( outptr, info->data[i].data );
			fields_setused( info, i );
			n++;
		}
	}
	newstr_free( &ntag );
	if ( n )
		fprintf( outptr, "</b:NameList></%s>\n", tag );
}

static void
output_names( fields *info, FILE *outptr, int level, int type )
{
	char *authors[] = { "AUTHOR", "WRITER", "ASSIGNEE", "ARTIST",
		"CARTOGRAPHER", "INVENTOR", "ORGANIZER", "DIRECTOR",
		"PERFORMER", "REPORTER", "TRANSLATOR", "RECIPIENT",
		"2ND_AUTHOR", "3RD_AUTHOR", "SUB_AUTHOR", "COMMITTEE",
		"COURT", "LEGISLATIVEBODY" };
	int nauthors = sizeof( authors ) / sizeof( authors[0] );

	char *editors[] = { "EDITOR" };
	int neditors = sizeof( editors ) / sizeof( editors[0] );

	char author_default[] = "b:Author", inventor[] = "b:Inventor";
	char *author_type = author_default;

	if ( type == TYPE_PATENT ) author_type = inventor;

	fprintf( outptr, "<b:Author>\n" );
	output_name_type( info, outptr, level, authors, nauthors, author_type );
	output_name_type( info, outptr, level, editors, neditors, "b:Editor" );
	fprintf( outptr, "</b:Author>\n" );
}

static void
output_date( fields *info, FILE *outptr, int level )
{
	char *year  = fields_findv_firstof( info, level, FIELDS_CHRP,
			"PARTYEAR", "YEAR", NULL );
	char *month = fields_findv_firstof( info, level, FIELDS_CHRP,
			"PARTMONTH", "MONTH", NULL );
	char *day   = fields_findv_firstof( info, level, FIELDS_CHRP,
			"PARTDAY", "DAY", NULL );
	if ( year )  output_itemv( outptr, "b:Year", year, 0 );
	if ( month ) output_itemv( outptr, "b:Month", month, 0 );
	if ( day )   output_itemv( outptr, "b:Day", day, 0 );
}

static void
output_pages( fields *info, FILE *outptr, int level )
{
	char *sn = fields_findv( info, LEVEL_ANY, FIELDS_CHRP, "PAGESTART" );
	char *en = fields_findv( info, LEVEL_ANY, FIELDS_CHRP, "PAGEEND" );
	char *ar = fields_findv( info, LEVEL_ANY, FIELDS_CHRP, "ARTICLENUMBER" );
	if ( sn || en )
		output_range( outptr, "b:Pages", sn, en, level );
	else if ( ar )
		output_range( outptr, "b:Pages", ar, NULL, level );
}

static void
output_includedin( fields *info, FILE *outptr, int type )
{
	if ( type==TYPE_JOURNALARTICLE ) {
		output_titleinfo( info, outptr, "b:JournalName", 1 );
	} else if ( type==TYPE_ARTICLEINAPERIODICAL ) {
		output_titleinfo( info, outptr, "b:PeriodicalTitle", 1 );
	} else if ( type==TYPE_BOOKSECTION ) {
		output_titleinfo( info, outptr, "b:ConferenceName", 1 ); /*??*/
	} else if ( type==TYPE_PROCEEDINGS ) {
		output_titleinfo( info, outptr, "b:ConferenceName", 1 );
	}
}

static int
type_is_thesis( int type )
{
	if ( type==TYPE_THESIS ||
	     type==TYPE_PHDTHESIS ||
	     type==TYPE_MASTERSTHESIS )
		return 1;
	else
		return 0;
}

static void
output_thesisdetails( fields *info, FILE *outptr, int type )
{
	char *tag;
	int i, n;

	if ( type==TYPE_PHDTHESIS )
		output_fixed( outptr, "b:ThesisType", "Ph.D. Thesis", 0 );
	else if ( type==TYPE_MASTERSTHESIS ) 
		output_fixed( outptr, "b:ThesisType", "Masters Thesis", 0 );

	n = fields_num( info );
	for ( i=0; i<n; ++i ) {
		tag = fields_tag( info, i, FIELDS_CHRP );
		if ( strcasecmp( tag, "DEGREEGRANTOR" ) &&
			strcasecmp( tag, "DEGREEGRANTOR:ASIS") &
			strcasecmp( tag, "DEGREEGRANTOR:CORP"))
				continue;
		output_item( info, outptr, "b:Institution", i, 0 );
	}
}

static
outtype types[] = {
	{ TYPE_UNKNOWN,                  "Misc" },
	{ TYPE_MISC,                     "Misc" },
	{ TYPE_BOOK,                     "Book" },
	{ TYPE_BOOKSECTION,              "BookSection" },
	{ TYPE_CASE,                     "Case" },
	{ TYPE_CONFERENCE,               "Conference" },
	{ TYPE_ELECTRONICSOURCE,         "ElectronicSource" },
	{ TYPE_FILM,                     "Film" },
	{ TYPE_INTERNETSITE,             "InternetSite" },
	{ TYPE_INTERVIEW,                "Interview" },
	{ TYPE_SOUNDRECORDING,           "SoundRecording" },
	{ TYPE_ARTICLEINAPERIODICAL,     "ArticleInAPeriodical" },
	{ TYPE_DOCUMENTFROMINTERNETSITE, "DocumentFromInternetSite" },
	{ TYPE_JOURNALARTICLE,           "JournalArticle" },
	{ TYPE_REPORT,                   "Report" },
	{ TYPE_PATENT,                   "Patent" },
	{ TYPE_PERFORMANCE,              "Performance" },
	{ TYPE_PROCEEDINGS,              "Proceedings" },
};
static
int ntypes = sizeof( types ) / sizeof( types[0] );

static void
output_type( fields *info, FILE *outptr, int type )
{
	int i, found = 0;
	fprintf( outptr, "<b:SourceType>" );
	for ( i=0; i<ntypes && !found; ++i ) {
		if ( types[i].value!=type ) continue;
		found = 1;
		fprintf( outptr, "%s", types[i].out );
	}
	if ( !found ) {
		if (  type_is_thesis( type ) ) fprintf( outptr, "Report" );
		else fprintf( outptr, "Misc" );
	}
	fprintf( outptr, "</b:SourceType>\n" );

	if ( type_is_thesis( type ) )
		output_thesisdetails( info, outptr, type );
}

static void
output_comments( fields *info, FILE *outptr, int level )
{
	vplist notes;
	char *abs;
	int i;

	vplist_init( &notes );

	abs = fields_findv( info, level, FIELDS_CHRP, "ABSTRACT" );
	fields_findv_each( info, level, FIELDS_CHRP, &notes, "NOTES" );

	if ( abs || notes.n ) fprintf( outptr, "<b:Comments>" );
	if ( abs ) fprintf( outptr, "%s", abs );
	for ( i=0; i<notes.n; ++i )
		fprintf( outptr, "%s", (char*)vplist_get( &notes, i ) );
	if ( abs || notes.n ) fprintf( outptr, "</b:Comments>\n" );

	vplist_free( &notes );
}

static void
output_bibkey( fields *info, FILE *outptr )
{
	char *bibkey = fields_findv_firstof( info, LEVEL_ANY, FIELDS_CHRP,
			"REFNUM", "BIBKEY", NULL );
	if ( bibkey ) output_itemv( outptr, "b:Tag", bibkey, 0 );
}

static void
output_citeparts( fields *info, FILE *outptr, int level, int max, int type )
{
	convert origin[] = {
		{ "ADDRESS",	"b:City",	LEVEL_ANY },
		{ "PUBLISHER",	"b:Publisher",	LEVEL_ANY },
		{ "EDITION",	"b:Edition",	LEVEL_ANY }
	};
	int norigin = sizeof( origin ) / sizeof ( convert );
	
	convert parts[] = {
		{ "VOLUME",          "b:Volume",  LEVEL_ANY },
		{ "SECTION",         "b:Section", LEVEL_ANY },
		{ "ISSUE",           "b:Issue",   LEVEL_ANY },
		{ "NUMBER",          "b:Issue",   LEVEL_ANY },
		{ "PUBLICLAWNUMBER", "b:Volume",  LEVEL_ANY },
		{ "SESSION",         "b:Issue",   LEVEL_ANY },
		{ "URL",             "b:Url",     LEVEL_ANY },
	};
	int nparts=sizeof(parts)/sizeof(convert);
	
	output_bibkey( info, outptr );
	output_type( info, outptr, type );
	output_list( info, outptr, origin, norigin );
	output_date( info, outptr, level );
	output_includedin( info, outptr, type );
	output_list( info, outptr, parts, nparts );
	output_pages( info, outptr, level );
	output_names( info, outptr, level, type );
	output_title( info, outptr, 0 );
	output_comments( info, outptr, level );
}

void
wordout_write( fields *info, FILE *outptr, param *p, unsigned long numrefs )
{
	int max = fields_maxlevel( info );
	int type = get_type( info );

	fprintf( outptr, "<b:Source>\n" );
	output_citeparts( info, outptr, -1, max, type );
	fprintf( outptr, "</b:Source>\n" );

	fflush( outptr );
}

void
wordout_writeheader( FILE *outptr, param *p )
{
	if ( p->utf8bom ) utf8_writebom( outptr );
	fprintf(outptr,"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	fprintf(outptr,"<b:Sources SelectedStyle=\"\" "
		"xmlns:b=\"http://schemas.openxmlformats.org/officeDocument/2006/bibliography\" "
		" xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/bibliography\" >\n");
}

void
wordout_writefooter( FILE *outptr )
{
	fprintf(outptr,"</b:Sources>\n");
	fflush( outptr );
}
