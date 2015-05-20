#ifndef SPARQL_H_INCLUDED
#define SPARQL_H_INCLUDED

#include <yaz/wrbuf.h>
#include <yaz/z-core.h>
#include <yaz/nmem.h>

YAZ_BEGIN_CDECL

typedef struct yaz_sparql_s *yaz_sparql_t;

YAZ_EXPORT
yaz_sparql_t yaz_sparql_create(void);

YAZ_EXPORT
void yaz_sparql_destroy(yaz_sparql_t s);

YAZ_EXPORT
int yaz_sparql_add_pattern(yaz_sparql_t s, const char *pattern,
                           const char *value);

YAZ_EXPORT
int yaz_sparql_from_rpn_stream(yaz_sparql_t s,
                               WRBUF addinfo,
                               void (*pr)(const char *buf,
                                          void *client_data),
                               void *client_data,
                               Z_RPNQuery *q);

YAZ_EXPORT
int yaz_sparql_from_rpn_wrbuf(yaz_sparql_t s, WRBUF addinfo, WRBUF w,
                              Z_RPNQuery *q);


YAZ_EXPORT
int yaz_sparql_from_uri_stream(yaz_sparql_t s,
                               WRBUF addinfo,
                               void (*pr)(const char *buf, void *client_data),
                               void *client_data,
                               const char *uri, const char *schema);

YAZ_EXPORT
int yaz_sparql_from_uri_wrbuf(yaz_sparql_t s, WRBUF addinfo, WRBUF w,
                              const char *uri, const char *schema);

YAZ_EXPORT
int yaz_sparql_lookup_schema(yaz_sparql_t s, const char *schema);

YAZ_EXPORT
void yaz_sparql_include(yaz_sparql_t s, yaz_sparql_t u);

YAZ_EXPORT
void yaz_sparql_explain_indexes( yaz_sparql_t s, WRBUF w, int indent);

YAZ_END_CDECL

#endif
/* CQL_H_INCLUDED */
/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

