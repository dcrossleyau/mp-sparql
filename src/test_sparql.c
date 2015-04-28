/* This file is part of the YAZ toolkit.
 * Copyright (C) Index Data
 * See the file LICENSE for details.
 */
#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "sparql.h"
#include <yaz/log.h>
#include <yaz/test.h>
#include <yaz/pquery.h>

static int test_query(yaz_sparql_t s, const char *pqf, const char *expect)
{
    YAZ_PQF_Parser parser = yaz_pqf_create();
    ODR odr = odr_createmem(ODR_ENCODE);
    Z_RPNQuery *rpn = yaz_pqf_parse(parser, odr, pqf);
    int ret = 0;
    WRBUF addinfo = wrbuf_alloc();
    WRBUF w = wrbuf_alloc();

    if (rpn)
    {
        int r = yaz_sparql_from_rpn_wrbuf(s, addinfo, w, rpn);
        if (expect)
        {
            if (!r)
            {
                if (!strcmp(expect, wrbuf_cstr(w)))
                    ret = 1;
                else
                {
                    yaz_log(YLOG_WARN, "test_sparql: pqf=%s", pqf);
                    yaz_log(YLOG_WARN, " expect: %s", expect);
                    yaz_log(YLOG_WARN, " got:    %s", wrbuf_cstr(w));
                }
            }
            else
            {
                yaz_log(YLOG_WARN, "test_sparql: pqf=%s", pqf);
                yaz_log(YLOG_WARN, " expect: %s", expect);
                yaz_log(YLOG_WARN, " got error: %d:%s", r, wrbuf_cstr(addinfo));
            }
        }
        else
        {
            if (r)
                ret = 1;
            else
            {
                yaz_log(YLOG_WARN, "test_sparql: pqf=%s", pqf);
                yaz_log(YLOG_WARN, " expect error");
                yaz_log(YLOG_WARN, " got:    %s", wrbuf_cstr(w));
            }
        }
    }
    wrbuf_destroy(w);
    wrbuf_destroy(addinfo);
    odr_destroy(odr);
    yaz_pqf_destroy(parser);
    return ret;
}

static int test_uri(yaz_sparql_t s, const char *uri, const char *schema,
                    const char *expect)
{
    int ret = 0;
    WRBUF addinfo = wrbuf_alloc();
    WRBUF w = wrbuf_alloc();

    int r = yaz_sparql_from_uri_wrbuf(s, addinfo, w, uri, schema);
    if (expect)
    {
        if (!r)
        {
            if (!strcmp(expect, wrbuf_cstr(w)))
                ret = 1;
            else
            {
                yaz_log(YLOG_WARN, "test_sparql: uri=%s", uri);
                yaz_log(YLOG_WARN, " expect: %s", expect);
                yaz_log(YLOG_WARN, " got:    %s", wrbuf_cstr(w));
            }
        }
        else
        {
            yaz_log(YLOG_WARN, "test_sparql: uri=%s", uri);
            yaz_log(YLOG_WARN, " expect: %s", expect);
            yaz_log(YLOG_WARN, " got error: %d:%s", r, wrbuf_cstr(addinfo));
        }
    }
    else
    {
        if (r)
            ret = 1;
        else
        {
            yaz_log(YLOG_WARN, "test_sparql: uri=%s", uri);
            yaz_log(YLOG_WARN, " expect error");
            yaz_log(YLOG_WARN, " got:    %s", wrbuf_cstr(w));
        }
    }
    wrbuf_destroy(w);
    wrbuf_destroy(addinfo);
    return ret;
}


static void tst1(void)
{
    yaz_sparql_t s = yaz_sparql_create();

    yaz_sparql_add_pattern(s, "prefix",
                           "rdf: http://www.w3.org/1999/02/22-rdf-syntax-ns");
    yaz_sparql_add_pattern(s, "prefix",
                           "bf: <http://bibframe.org/vocab/>");
    yaz_sparql_add_pattern(s, "prefix",
                           "gs: http://gs.com/panorama/domain-model");
    yaz_sparql_add_pattern(s, "form", "SELECT ?title ?author ?description ?ititle");
    yaz_sparql_add_pattern(s, "criteria", "?work a bf:Work");
    yaz_sparql_add_pattern(s, "criteria", "?work bf:workTitle/bf:titleValue ?title");
    yaz_sparql_add_pattern(s, "criteria", "?work bf:creator/bf:label ?author");
    yaz_sparql_add_pattern(s, "criteria", "?work bf:note ?description");
    yaz_sparql_add_pattern(s, "criteria", "?inst bf:instanceOf ?work");
    yaz_sparql_add_pattern(s, "criteria", "?inst bf:instanceTitle/bf:titleValue ?ititle");
    yaz_sparql_add_pattern(s, "criteria.optional", "?inst bf:heldBy ?lib");

    yaz_sparql_add_pattern(s, "index.bf.title",
                           "?work bf:workTitle/bf:titleValue ?o1 "
                           "FILTER(contains(?o1, %s))");
    yaz_sparql_add_pattern(s, "index.bf.creator",
                           "?work bf:creator/bf:label ?o2 "
                           "FILTER(contains(?o2, %s))");
    yaz_sparql_add_pattern(s, "index.bf.authorityCreator",
                           "?work bf:author %s");
    yaz_sparql_add_pattern(s, "index.bf.type",
                           "?inst rdf:type %s");
    yaz_sparql_add_pattern(s, "index.bf.format",
                           "?inst bf:format ?o5 FILTER(contains(?o5, %s))");
    yaz_sparql_add_pattern(s, "index.bf.nearby", "?lib gs:nearby (%d)");
    yaz_sparql_add_pattern(s, "index.bf.baseTitle",
                           "?work bf:derivativeOf/bf:workTitle/bf:titleValue "
                           "?o6 FILTER(contains(?o6, %s))");
    yaz_sparql_add_pattern(s, "index.bf.baseCreator",
                           "?work bf:derivativeOf/bf:creator/bf:label "
                           "?o7 FILTER(contains(?o7, %s))");
    yaz_sparql_add_pattern(s, "index.bf.targetAudience",
                           "?work bf:targetAudience %s");
    yaz_sparql_add_pattern(s, "index.bf.isbn", "?inst bf:ISBN %s");

    yaz_sparql_add_pattern(s, "uri.full", "SELECT ?sub ?rel WHERE ?work = %u");

    YAZ_CHECK(test_uri(s, "http://x/y", "full",
                       "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                       "PREFIX bf: <http://bibframe.org/vocab/>\n"
                       "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                       "SELECT ?sub ?rel WHERE ?work = <http://x/y>\n"));

    YAZ_CHECK(test_query(
                  s, "@attr 1=bf.title computer",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"computer\"))\n"
                  "}\n"
                  ));

    YAZ_CHECK(test_query(
                  s, "@attr 1=bf.creator london",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  ?work bf:creator/bf:label ?o2 "
                  "FILTER(contains(?o2, \"london\"))\n"
                  "}\n"));


    YAZ_CHECK(test_query(
                  s, "@and @attr 1=bf.creator london @attr 1=bf.title computer",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  ?work bf:creator/bf:label ?o2 "
                  "FILTER(contains(?o2, \"london\")) .\n"
                  "  ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"computer\"))\n"
                  "}\n"));

    YAZ_CHECK(test_query(
                  s, "@or @attr 1=bf.creator london @attr 1=bf.title computer",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  {\n"
                  "   ?work bf:creator/bf:label ?o2 "
                  "FILTER(contains(?o2, \"london\"))\n"
                  "  } UNION {\n"
                  "   ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"computer\"))\n"
                  "  }\n"
                  "}\n"
                  ));

    YAZ_CHECK(test_query(
                  s, "@or @or @attr 1=bf.creator a @attr 1=bf.title b @attr 1=bf.title c",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  {\n"
                  "   {\n"
                  "    ?work bf:creator/bf:label ?o2 "
                  "FILTER(contains(?o2, \"a\"))\n"
                  "   } UNION {\n"
                  "    ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"b\"))\n"
                  "   }\n"
                  "  } UNION {\n"
                  "   ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"c\"))\n"
                  "  }\n"
                  "}\n"
                  ));

    YAZ_CHECK(test_query(
                  s, "@or @and @attr 1=bf.creator a @attr 1=bf.title b @attr 1=bf.title c",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  {\n"
                  "   ?work bf:creator/bf:label ?o2 "
                  "FILTER(contains(?o2, \"a\")) .\n"
                  "   ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"b\"))\n"
                  "  } UNION {\n"
                  "   ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"c\"))\n"
                  "  }\n"
                  "}\n"
                  ));

    YAZ_CHECK(test_query(
                  s, "@and @and @attr 1=bf.creator a @attr 1=bf.title b @attr 1=bf.title c",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  ?work bf:creator/bf:label ?o2 "
                  "FILTER(contains(?o2, \"a\")) .\n"
                  "  ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"b\")) .\n"
                  "  ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"c\"))\n"
                  "}\n"
                  ));

    YAZ_CHECK(test_query(
                  s, "@and @attr 1=bf.title \"Phantom Tollbooth\" "
                  "@attr 1=bf.nearby \"40.1583 83.0742 30\"",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  ?inst bf:heldBy ?lib .\n"
                  "  ?work bf:workTitle/bf:titleValue ?o1 "
                  "FILTER(contains(?o1, \"Phantom Tollbooth\")) .\n"
                  "  ?lib gs:nearby (40.1583 83.0742 30)\n"
                  "}\n"
                  ));

    YAZ_CHECK(test_query(
                  s, "@attr 1=bf.isbn 9780316154697",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  ?inst bf:ISBN \"9780316154697\"\n"
                  "}\n"
                 ));


    yaz_sparql_destroy(s);
}

static void tst2(void)
{
    yaz_sparql_t s = yaz_sparql_create();

    yaz_sparql_add_pattern(s, "prefix",
                           "rdf: http://www.w3.org/1999/02/22-rdf-syntax-ns");
    yaz_sparql_add_pattern(s, "prefix",
                           "bf: <http://bibframe.org/vocab/>");
    yaz_sparql_add_pattern(s, "prefix",
                           "gs: http://gs.com/panorama/domain-model");
    yaz_sparql_add_pattern(s, "form", "SELECT ?title ?author ?description ?ititle");
    yaz_sparql_add_pattern(s, "criteria", "?work a bf:Work");
    yaz_sparql_add_pattern(s, "criteria", "?work bf:workTitle/bf:titleValue ?title");
    yaz_sparql_add_pattern(s, "criteria", "?work bf:creator/bf:label ?author");
    yaz_sparql_add_pattern(s, "criteria", "?work bf:note ?description");
    yaz_sparql_add_pattern(s, "criteria", "?inst bf:instanceOf ?work");
    yaz_sparql_add_pattern(s, "criteria", "?inst bf:instanceTitle/bf:titleValue ?ititle");
    yaz_sparql_add_pattern(s, "criteria.optional", "?inst bf:heldBy ?lib");

    yaz_sparql_add_pattern(s, "index.bf.title",
                           "?work bf:workTitle/bf:titleValue %v "
                           "FILTER(contains(%v, %s))");
    yaz_sparql_add_pattern(s, "index.bf.creator",
                           "?work bf:creator/bf:label %v "
                           "FILTER(contains(%v, %s))");
    yaz_sparql_add_pattern(s, "index.bf.authorityCreator",
                           "?work bf:author %s");
    yaz_sparql_add_pattern(s, "index.bf.type", "?inst rdf:type %s");
    yaz_sparql_add_pattern(s, "index.bf.format",
                           "?inst bf:format %v FILTER(contains(%v, %s))");
    yaz_sparql_add_pattern(s, "index.bf.nearby", "?lib gs:nearby (%d)");
    yaz_sparql_add_pattern(s, "index.bf.baseTitle",
                           "?work bf:derivativeOf/bf:workTitle/bf:titleValue "
                           "%v FILTER(contains(%v, %s))");
    yaz_sparql_add_pattern(s, "index.bf.baseCreator",
                           "?work bf:derivativeOf/bf:creator/bf:label "
                           "%v FILTER(contains(%v, %s))");
    yaz_sparql_add_pattern(s, "index.bf.targetAudience",
                           "?work bf:targetAudience %s");
    yaz_sparql_add_pattern(s, "index.bf.isbn", "?inst bf:ISBN %s");

    YAZ_CHECK(test_query(
                  s, "@attr 1=bf.title computer",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  ?work bf:workTitle/bf:titleValue ?v0 "
                  "FILTER(contains(?v0, \"computer\"))\n"
                  "}\n"
                  ));

    YAZ_CHECK(test_query(
                  s, "@attr 1=bf.creator london",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  ?work bf:creator/bf:label ?v0 "
                  "FILTER(contains(?v0, \"london\"))\n"
                  "}\n"));

    YAZ_CHECK(test_query(
                  s, "@or @and @attr 1=bf.creator a @attr 1=bf.title b @attr 1=bf.title c",
                  "PREFIX rdf: <http://www.w3.org/1999/02/22-rdf-syntax-ns>\n"
                  "PREFIX bf: <http://bibframe.org/vocab/>\n"
                  "PREFIX gs: <http://gs.com/panorama/domain-model>\n"
                  "SELECT ?title ?author ?description ?ititle\n"
                  "WHERE {\n"
                  "  ?work a bf:Work .\n"
                  "  ?work bf:workTitle/bf:titleValue ?title .\n"
                  "  ?work bf:creator/bf:label ?author .\n"
                  "  ?work bf:note ?description .\n"
                  "  ?inst bf:instanceOf ?work .\n"
                  "  ?inst bf:instanceTitle/bf:titleValue ?ititle .\n"
                  "  OPTIONAL { ?inst bf:heldBy ?lib } .\n"
                  "  {\n"
                  "   ?work bf:creator/bf:label ?v0 "
                  "FILTER(contains(?v0, \"a\")) .\n"
                  "   ?work bf:workTitle/bf:titleValue ?v1 "
                  "FILTER(contains(?v1, \"b\"))\n"
                  "  } UNION {\n"
                  "   ?work bf:workTitle/bf:titleValue ?v2 "
                  "FILTER(contains(?v2, \"c\"))\n"
                  "  }\n"
                  "}\n"
                  ));

    yaz_sparql_destroy(s);
}

int main(int argc, char **argv)
{
    YAZ_CHECK_INIT(argc, argv);
    YAZ_CHECK_LOG();
    tst1();
    tst2();
    YAZ_CHECK_TERM;
}
/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

