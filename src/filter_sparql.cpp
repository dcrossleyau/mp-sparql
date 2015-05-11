/* This file is part of Metaproxy.
   Copyright (C) Index Data

Metaproxy is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

Metaproxy is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <metaproxy/package.hpp>
#include <metaproxy/util.hpp>
#include <yaz/log.h>
#include <yaz/srw.h>
#include <yaz/diagbib1.h>
#include <yaz/match_glob.h>
#include <boost/scoped_ptr.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/algorithm/string.hpp>
#include "sparql.h"

#include <yaz/zgdu.h>

namespace mp = metaproxy_1;
namespace yf = mp::filter;

namespace metaproxy_1 {
    namespace filter {
        class SPARQL : public Base {
            class Session;
            class Rep;
            class Conf;
            class Result;
            class FrontendSet;

            typedef boost::shared_ptr<Session> SessionPtr;
            typedef boost::shared_ptr<Conf> ConfPtr;

            typedef boost::shared_ptr<FrontendSet> FrontendSetPtr;
            typedef std::map<std::string,FrontendSetPtr> FrontendSets;
        public:
            SPARQL();
            ~SPARQL();
            void process(metaproxy_1::Package & package) const;
            void configure(const xmlNode * ptr, bool test_only,
                           const char *path);
            SessionPtr get_session(Package &package, Z_APDU **apdu) const;
            void release_session(Package &package) const;
            boost::scoped_ptr<Rep> m_p;
            std::list<ConfPtr> db_conf;
        };
        class SPARQL::Conf {
        public:
            std::string db;
            std::string uri;
            std::string schema;
            yaz_sparql_t s;
            ~Conf();
        };
        class SPARQL::Rep {
            friend class SPARQL;
            boost::condition m_cond_session_ready;
            boost::mutex m_mutex;
            std::map<mp::Session,SessionPtr> m_clients;
        };
        class SPARQL::Result {
        public:
            Result();
            ~Result();
        private:
            friend class FrontendSet;
            friend class Session;
            ConfPtr conf;
            xmlDoc *doc;
        };
        class SPARQL::FrontendSet {
        private:
            friend class Session;
            Odr_int hits;
            std::string db;
            std::list<Result> results;
        };
        class SPARQL::Session {
        public:
            Session(const SPARQL *);
            ~Session();
            void handle_z(Package &package, Z_APDU *apdu);
            Z_APDU *search(mp::Package &package,
                           Z_APDU *apdu_req,
                           mp::odr &odr,
                           const char *sparql_query,
                           ConfPtr conf, FrontendSetPtr fset);
            int invoke_sparql(mp::Package &package,
                              const char *sparql_query,
                              ConfPtr conf,
                              WRBUF w);

            Z_Records *fetch(
                Package &package,
                FrontendSetPtr fset,
                ODR odr, Odr_oid *preferredRecordSyntax,
                Z_ElementSetNames *esn,
                int start, int number, int &error_code, std::string &addinfo,
                int *number_returned, int *next_position);
            bool m_in_use;
        private:
            bool m_support_named_result_sets;
            FrontendSets m_frontend_sets;
            const SPARQL *m_sparql;
        };
    }
}

yf::SPARQL::Result::~Result()
{
    if (doc)
        xmlFreeDoc(doc);
}

yf::SPARQL::Result::Result()
{
    doc = 0;
}

yf::SPARQL::SPARQL() : m_p(new Rep)
{
}

yf::SPARQL::~SPARQL()
{
}

void yf::SPARQL::configure(const xmlNode *xmlnode, bool test_only,
                           const char *path)
{
    const xmlNode *ptr = xmlnode->children;
    std::string uri;

    for (; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        if (!strcmp((const char *) ptr->name, "defaults"))
        {
            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "uri"))
                    uri = mp::xml::get_text(attr->children);
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
        }
        else if (!strcmp((const char *) ptr->name, "db"))
        {
            yaz_sparql_t s = yaz_sparql_create();
            ConfPtr conf(new Conf);
            conf->s = s;
            conf->uri = uri;

            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "path"))
                    conf->db = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "uri"))
                    conf->uri = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "schema"))
                    conf->schema = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "include"))
                {
                    std::vector<std::string> dbs;
                    std::string db = mp::xml::get_text(attr->children);
                    boost::split(dbs, db, boost::is_any_of(" \t"));
                    size_t i;
                    for (i = 0; i < dbs.size(); i++)
                    {
                        if (dbs[i].length() == 0)
                            continue;
                        std::list<ConfPtr>::const_iterator it = db_conf.begin();
                        while (1)
                            if (it == db_conf.end())
                            {
                                throw mp::filter::FilterException(
                                    "include db not found: " + dbs[i]);
                            }
                            else if (dbs[i].compare((*it)->db) == 0)
                            {
                                yaz_sparql_include(s, (*it)->s);
                                break;
                            }
                            else
                                it++;
                    }
                }
                else
                    throw mp::filter::FilterException(
                        "Bad attribute " + std::string((const char *)
                                                       attr->name));
            }
            xmlNode *p = ptr->children;
            for (; p; p = p->next)
            {
                if (p->type != XML_ELEMENT_NODE)
                    continue;
                std::string name = (const char *) p->name;
                const struct _xmlAttr *attr;
                for (attr = p->properties; attr; attr = attr->next)
                {
                    if (!strcmp((const char *) attr->name, "type"))
                    {
                        name.append(".");
                        name.append(mp::xml::get_text(attr->children));
                    }
                    else
                        throw mp::filter::FilterException(
                            "Bad attribute " + std::string((const char *)
                                                           attr->name));
                }
                std::string value = mp::xml::get_text(p);
                if (yaz_sparql_add_pattern(s, name.c_str(), value.c_str()))
                {
                    throw mp::filter::FilterException(
                        "Bad SPARQL config " + name);
                }
            }
            if (!conf->uri.length())
            {
                throw mp::filter::FilterException("Missing uri");
            }
            if (!conf->db.length())
            {
                throw mp::filter::FilterException("Missing path");
            }
            db_conf.push_back(conf);
        }
        else
        {
            throw mp::filter::FilterException
                ("Bad element "
                 + std::string((const char *) ptr->name)
                 + " in sparql filter");
        }
    }
}

yf::SPARQL::Conf::~Conf()
{
    yaz_sparql_destroy(s);
}

yf::SPARQL::Session::Session(const SPARQL *sparql) :
    m_in_use(true),
    m_support_named_result_sets(false),
    m_sparql(sparql)
{
}

yf::SPARQL::Session::~Session()
{
}

yf::SPARQL::SessionPtr yf::SPARQL::get_session(Package & package,
                                               Z_APDU **apdu) const
{
    SessionPtr ptr0;

    Z_GDU *gdu = package.request().get();

    boost::mutex::scoped_lock lock(m_p->m_mutex);

    std::map<mp::Session,SPARQL::SessionPtr>::iterator it;

    if (gdu && gdu->which == Z_GDU_Z3950)
        *apdu = gdu->u.z3950;
    else
        *apdu = 0;

    while (true)
    {
        it = m_p->m_clients.find(package.session());
        if (it == m_p->m_clients.end())
            break;
        if (!it->second->m_in_use)
        {
            it->second->m_in_use = true;
            return it->second;
        }
        m_p->m_cond_session_ready.wait(lock);
    }
    if (!*apdu)
        return ptr0;

    // new Z39.50 session ..
    SessionPtr p(new Session(this));
    m_p->m_clients[package.session()] = p;
    return p;
}

void yf::SPARQL::release_session(Package &package) const
{
    boost::mutex::scoped_lock lock(m_p->m_mutex);
    std::map<mp::Session,SessionPtr>::iterator it;

    it = m_p->m_clients.find(package.session());
    if (it != m_p->m_clients.end())
    {
        it->second->m_in_use = false;

        if (package.session().is_closed())
            m_p->m_clients.erase(it);
        m_p->m_cond_session_ready.notify_all();
    }
}

static bool get_result(xmlDoc *doc, Odr_int *sz, Odr_int pos, xmlDoc **ndoc)
{
    xmlNode *ptr = xmlDocGetRootElement(doc);
    xmlNode *q0;
    Odr_int cur = 0;

    if (ndoc)
        *ndoc = xmlNewDoc(BAD_CAST "1.0");

    if (ptr->type == XML_ELEMENT_NODE &&
        !strcmp((const char *) ptr->name, "RDF"))
    {
        if (ndoc)
        {
            q0 = xmlCopyNode(ptr, 2);
            xmlDocSetRootElement(*ndoc, q0);
        }
        ptr = ptr->children;

        while (ptr && ptr->type != XML_ELEMENT_NODE)
            ptr = ptr->next;
        if (ptr && ptr->type == XML_ELEMENT_NODE &&
            !strcmp((const char *) ptr->name, "Description"))
        {
            xmlNode *p = ptr->children;

            while (p && p->type != XML_ELEMENT_NODE)
                p = p->next;
            if (p && p->type == XML_ELEMENT_NODE &&
                !strcmp((const char *) p->name, "type"))
            { /* SELECT RESULT */
                for (ptr = ptr->children; ptr; ptr = ptr->next)
                    if (ptr->type == XML_ELEMENT_NODE &&
                        !strcmp((const char *) ptr->name, "solution"))
                    {
                        if (cur++ == pos)
                        {
                            if (ndoc)
                            {
                                xmlNode *q1 = xmlCopyNode(ptr, 1);
                                xmlAddChild(q0, q1);
                            }
                            break;
                        }
                    }
            }
            else
            {   /* CONSTRUCT result */
                for (; ptr; ptr = ptr->next)
                    if (ptr->type == XML_ELEMENT_NODE &&
                        !strcmp((const char *) ptr->name, "Description"))
                    {
                        if (cur++ == pos)
                        {
                            if (ndoc)
                            {
                                xmlNode *q1 = xmlCopyNode(ptr, 1);
                                xmlAddChild(q0, q1);
                            }
                            return true;
                        }
                    }
            }
        }
    }
    else
    {
        for (; ptr; ptr = ptr->next)
            if (ptr->type == XML_ELEMENT_NODE &&
                !strcmp((const char *) ptr->name, "sparql"))
                break;
        if (ptr)
        {
            if (ndoc)
            {
                q0 = xmlCopyNode(ptr, 2);
                xmlDocSetRootElement(*ndoc, q0);
            }
            for (ptr = ptr->children; ptr; ptr = ptr->next)
                if (ptr->type == XML_ELEMENT_NODE &&
                    !strcmp((const char *) ptr->name, "results"))
                    break;
        }
        if (ptr)
        {
            xmlNode *q1 = 0;
            if (ndoc)
            {
                q1 = xmlCopyNode(ptr, 0);
                xmlAddChild(q0, q1);
            }
            for (ptr = ptr->children; ptr; ptr = ptr->next)
                if (ptr->type == XML_ELEMENT_NODE &&
                    !strcmp((const char *) ptr->name, "result"))
                {
                    if (cur++ == pos)
                    {
                        if (ndoc)
                        {
                            xmlNode *q2 = xmlCopyNode(ptr, 1);
                            xmlAddChild(q1, q2);
                        }
                        return true;
                    }
                }
        }
    }
    if (sz)
        *sz = cur;
    return false;
}

Z_Records *yf::SPARQL::Session::fetch(
    Package &package,
    FrontendSetPtr fset,
    ODR odr, Odr_oid *preferredRecordSyntax,
    Z_ElementSetNames *esn,
    int start, int number, int &error_code, std::string &addinfo,
    int *number_returned, int *next_position)
{
    Z_Records *rec = (Z_Records *) odr_malloc(odr, sizeof(Z_Records));
    std::list<Result>::iterator it = fset->results.begin();
    const char *schema = 0;
    bool uri_lookup = false;
    if (esn && esn->which == Z_ElementSetNames_generic)
        schema = esn->u.generic;

    for (; it != fset->results.end(); it++)
    {
        if (yaz_sparql_lookup_schema(it->conf->s, schema))
        {
            uri_lookup = true;
            break;
        }
        if (!schema || !strcmp(esn->u.generic, it->conf->schema.c_str()))
            break;
    }
    if (it == fset->results.end())
    {
        rec->which = Z_Records_NSD;
        rec->u.nonSurrogateDiagnostic =
            zget_DefaultDiagFormat(
                odr,
                YAZ_BIB1_SPECIFIED_ELEMENT_SET_NAME_NOT_VALID_FOR_SPECIFIED_,
                schema);
        return rec;
    }
    rec->which = Z_Records_DBOSD;
    rec->u.databaseOrSurDiagnostics = (Z_NamePlusRecordList *)
        odr_malloc(odr, sizeof(Z_NamePlusRecordList));
    rec->u.databaseOrSurDiagnostics->records = (Z_NamePlusRecord **)
        odr_malloc(odr, sizeof(Z_NamePlusRecord *) * number);
    int i;
    for (i = 0; i < number; i++)
    {
        rec->u.databaseOrSurDiagnostics->records[i] = (Z_NamePlusRecord *)
            odr_malloc(odr, sizeof(Z_NamePlusRecord));
        Z_NamePlusRecord *npr = rec->u.databaseOrSurDiagnostics->records[i];
        npr->databaseName = odr_strdup(odr, fset->db.c_str());
        npr->which = Z_NamePlusRecord_databaseRecord;
        xmlDoc *ndoc = 0;

        if (!get_result(it->doc, 0, start - 1 + i, &ndoc))
        {
            if (ndoc)
                xmlFreeDoc(ndoc);
            break;
        }
        xmlNode *ndoc_root = xmlDocGetRootElement(ndoc);
        if (!ndoc_root)
        {
            xmlFreeDoc(ndoc);
            break;
        }
        if (uri_lookup)
        {
            std::string uri;
            xmlNode *n = ndoc_root;
            while (n)
            {
                if (n->type == XML_ELEMENT_NODE)
                {
                    //if (!strcmp((const char *) n->name, "uri"))
                    if (!strcmp((const char *) n->name, "uri") ||
                        !strcmp((const char *) n->name, "bnode") )
                    {
                        uri = mp::xml::get_text(n->children);

                    }
                    n = n->children;
                }
                else
                    n = n->next;
            }
            if (!uri.length())
            {
                rec->which = Z_Records_NSD;
                rec->u.nonSurrogateDiagnostic =
                    zget_DefaultDiagFormat(
                        odr,
                        YAZ_BIB1_SYSTEM_ERROR_IN_PRESENTING_RECORDS, 0);
                xmlFreeDoc(ndoc);
                return rec;
            }
            else
            {
                mp::wrbuf addinfo, query, w;
                int error = yaz_sparql_from_uri_wrbuf(it->conf->s,
                                                      addinfo, query,
                                                      uri.c_str(), schema);
                if (!error)
                {
                    yaz_log(YLOG_LOG, "query=%s", query.c_str());
                    error = invoke_sparql(package, query.c_str(),
                                          it->conf, w);
                }
                if (error)
                {
                    rec->which = Z_Records_NSD;
                    rec->u.nonSurrogateDiagnostic =
                        zget_DefaultDiagFormat(
                            odr,
                            error,
                            addinfo.len() ? addinfo.c_str() : 0);
                    xmlFreeDoc(ndoc);
                    return rec;
                }
                npr->u.databaseRecord =
                    z_ext_record_xml(odr, w.c_str(), w.len());
            }
        }
        else
        {
            xmlBufferPtr buf = xmlBufferCreate();
            xmlNodeDump(buf, ndoc, ndoc_root, 0, 0);
            yaz_log(YLOG_LOG, "record %s %.*s", uri_lookup ? "uri" : "normal",
                    (int) buf->use, (const char *) buf->content);
            npr->u.databaseRecord =
                z_ext_record_xml(odr, (const char *) buf->content, buf->use);
            xmlBufferFree(buf);
        }
        xmlFreeDoc(ndoc);
    }
    rec->u.databaseOrSurDiagnostics->num_records = i;
    *number_returned = i;
    if (start + number > fset->hits)
        *next_position = 0;
    else
        *next_position = start + number;
    return rec;
}

int yf::SPARQL::Session::invoke_sparql(mp::Package &package,
                                       const char *sparql_query,
                                       ConfPtr conf,
                                       WRBUF w)
{
    Package http_package(package.session(), package.origin());
    mp::odr odr;

    http_package.copy_filter(package);
    Z_GDU *gdu = z_get_HTTP_Request_uri(odr, conf->uri.c_str(), 0, 1);

    z_HTTP_header_add(odr, &gdu->u.HTTP_Request->headers,
                      "Content-Type", "application/x-www-form-urlencoded");
    z_HTTP_header_add(odr, &gdu->u.HTTP_Request->headers,
                      "Accept", "application/sparql-results+xml,"
                      "application/rdf+xml");
    const char *names[2];
    names[0] = "query";
    names[1] = 0;
    const char *values[1];
    values[0] = sparql_query;
    char *path = 0;
    yaz_array_to_uri(&path, odr, (char **) names, (char **) values);

    gdu->u.HTTP_Request->content_buf = path;
    gdu->u.HTTP_Request->content_len = strlen(path);

    yaz_log(YLOG_LOG, "sparql: HTTP request\n%s", sparql_query);

    http_package.request() = gdu;
    http_package.move();

    Z_GDU *gdu_resp = http_package.response().get();

    if (!gdu_resp || gdu_resp->which != Z_GDU_HTTP_Response)
    {
        wrbuf_puts(w, "no HTTP response from backend");
        return YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
    }
    else if (gdu_resp->u.HTTP_Response->code != 200)
    {
        wrbuf_printf(w, "sparql: HTTP error %d from backend",
                     gdu_resp->u.HTTP_Response->code);
        return YAZ_BIB1_TEMPORARY_SYSTEM_ERROR;
    }
    Z_HTTP_Response *resp = gdu_resp->u.HTTP_Response;
    wrbuf_write(w, resp->content_buf, resp->content_len);
    return 0;
}

Z_APDU *yf::SPARQL::Session::search(mp::Package &package,
                                    Z_APDU *apdu_req,
                                    mp::odr &odr,
                                    const char *sparql_query,
                                    ConfPtr conf, FrontendSetPtr fset)
{
    Z_SearchRequest *req = apdu_req->u.searchRequest;
    Z_APDU *apdu_res = 0;
    mp::wrbuf w;

    int error = invoke_sparql(package, sparql_query, conf, w);
    if (error)
    {
        apdu_res = odr.create_searchResponse(apdu_req, error,
                                             w.len() ?
                                             w.c_str() : 0);
    }
    else
    {
        xmlDocPtr doc = xmlParseMemory(w.c_str(), w.len());
        if (!doc)
        {
            apdu_res = odr.create_searchResponse(
                apdu_req,
                YAZ_BIB1_TEMPORARY_SYSTEM_ERROR,
                "invalid XML from backendbackend");
        }
        else
        {
            Result result;
            Z_Records *records = 0;
            int number_returned = 0;
            int next_position = 0;
            int error_code = 0;
            std::string addinfo;

            result.doc = doc;
            result.conf = conf;
            fset->results.push_back(result);
            yaz_log(YLOG_LOG, "saving sparql result xmldoc=%p", doc);

            get_result(result.doc, &fset->hits, -1, 0);
            m_frontend_sets[req->resultSetName] = fset;

            result.doc = 0;

            Odr_int number = 0;
            const char *element_set_name = 0;
            mp::util::piggyback_sr(req, fset->hits, number, &element_set_name);
            if (number)
            {
                Z_ElementSetNames *esn;

                if (number > *req->smallSetUpperBound)
                    esn = req->mediumSetElementSetNames;
                else
                    esn = req->smallSetElementSetNames;
                records = fetch(package, fset,
                                odr, req->preferredRecordSyntax, esn,
                                1, number,
                                error_code, addinfo,
                                &number_returned,
                                &next_position);
            }
            if (error_code)
            {
                apdu_res =
                    odr.create_searchResponse(
                        apdu_req, error_code, addinfo.c_str());
            }
            else
            {
                apdu_res =
                    odr.create_searchResponse(apdu_req, 0, 0);
                Z_SearchResponse *resp = apdu_res->u.searchResponse;
                *resp->resultCount = fset->hits;
                *resp->numberOfRecordsReturned = number_returned;
                *resp->nextResultSetPosition = next_position;
                resp->records = records;
            }
        }
    }
    return apdu_res;
}

void yf::SPARQL::Session::handle_z(mp::Package &package, Z_APDU *apdu_req)
{
    mp::odr odr;
    Z_APDU *apdu_res = 0;
    if (apdu_req->which == Z_APDU_initRequest)
    {
        apdu_res = odr.create_initResponse(apdu_req, 0, 0);
        Z_InitRequest *req = apdu_req->u.initRequest;
        Z_InitResponse *resp = apdu_res->u.initResponse;

        resp->implementationName = odr_strdup(odr, "sparql");
        if (ODR_MASK_GET(req->options, Z_Options_namedResultSets))
            m_support_named_result_sets = true;
        int i;
        static const int masks[] = {
            Z_Options_search, Z_Options_present,
            Z_Options_namedResultSets, -1
        };
        for (i = 0; masks[i] != -1; i++)
            if (ODR_MASK_GET(req->options, masks[i]))
                ODR_MASK_SET(resp->options, masks[i]);
        static const int versions[] = {
            Z_ProtocolVersion_1,
            Z_ProtocolVersion_2,
            Z_ProtocolVersion_3,
            -1
        };
        for (i = 0; versions[i] != -1; i++)
            if (ODR_MASK_GET(req->protocolVersion, versions[i]))
                ODR_MASK_SET(resp->protocolVersion, versions[i]);
            else
                break;
        *resp->preferredMessageSize = *req->preferredMessageSize;
        *resp->maximumRecordSize = *req->maximumRecordSize;
    }
    else if (apdu_req->which == Z_APDU_close)
    {
        apdu_res = odr.create_close(apdu_req,
                                    Z_Close_finished, 0);
        package.session().close();
    }
    else if (apdu_req->which == Z_APDU_searchRequest)
    {
        Z_SearchRequest *req = apdu_req->u.searchRequest;

        FrontendSets::iterator fset_it =
            m_frontend_sets.find(req->resultSetName);
        if (fset_it != m_frontend_sets.end())
        {
            // result set already exist
            // if replace indicator is off: we return diagnostic if
            // result set already exist.
            if (*req->replaceIndicator == 0)
            {
                Z_APDU *apdu =
                    odr.create_searchResponse(
                        apdu_req,
                        YAZ_BIB1_RESULT_SET_EXISTS_AND_REPLACE_INDICATOR_OFF,
                        0);
                package.response() = apdu;
            }
            m_frontend_sets.erase(fset_it);
        }
        if (req->query->which != Z_Query_type_1)
        {
            apdu_res = odr.create_searchResponse(
                apdu_req, YAZ_BIB1_QUERY_TYPE_UNSUPP, 0);
        }
        else if (req->num_databaseNames != 1)
        {
            apdu_res = odr.create_searchResponse(
                apdu_req,
                YAZ_BIB1_ACCESS_TO_SPECIFIED_DATABASE_DENIED, 0);
        }
        else
        {
            std::string db = req->databaseNames[0];
            std::list<ConfPtr>::const_iterator it;
            FrontendSetPtr fset(new FrontendSet);

            m_frontend_sets.erase(req->resultSetName);
            fset->db = db;
            it = m_sparql->db_conf.begin();
            for (; it != m_sparql->db_conf.end(); it++)
                if ((*it)->schema.length() > 0
                    && yaz_match_glob((*it)->db.c_str(), db.c_str()))
                {
                    mp::wrbuf addinfo_wr;
                    mp::wrbuf sparql_wr;
                    int error =
                        yaz_sparql_from_rpn_wrbuf((*it)->s,
                                                  addinfo_wr, sparql_wr,
                                                  req->query->u.type_1);
                    if (error)
                    {
                        apdu_res = odr.create_searchResponse(
                            apdu_req, error,
                            addinfo_wr.len() ? addinfo_wr.c_str() : 0);
                    }
                    else
                    {
                        Z_APDU *apdu_1 = search(package, apdu_req, odr,
                                                sparql_wr.c_str(), *it,
                                                fset);
                        if (!apdu_res)
                            apdu_res = apdu_1;
                    }
                }
            if (apdu_res == 0)
            {
                apdu_res = odr.create_searchResponse(
                    apdu_req, YAZ_BIB1_DATABASE_DOES_NOT_EXIST, db.c_str());
            }
        }
    }
    else if (apdu_req->which == Z_APDU_presentRequest)
    {
        Z_PresentRequest *req = apdu_req->u.presentRequest;
        FrontendSets::iterator fset_it =
            m_frontend_sets.find(req->resultSetId);
        if (fset_it == m_frontend_sets.end())
        {
            apdu_res =
                odr.create_presentResponse(
                    apdu_req, YAZ_BIB1_SPECIFIED_RESULT_SET_DOES_NOT_EXIST,
                    req->resultSetId);
            package.response() = apdu_res;
            return;
        }
        int number_returned = 0;
        int next_position = 0;
        int error_code = 0;
        std::string addinfo;
        Z_ElementSetNames *esn = 0;
        if (req->recordComposition)
        {
            if (req->recordComposition->which == Z_RecordComp_simple)
                esn = req->recordComposition->u.simple;
            else
            {
                apdu_res =
                    odr.create_presentResponse(
                        apdu_req,
                        YAZ_BIB1_ONLY_A_SINGLE_ELEMENT_SET_NAME_SUPPORTED,
                        0);
                package.response() = apdu_res;
                return;
            }
        }
        Z_Records *records = fetch(
            package,
            fset_it->second,
            odr, req->preferredRecordSyntax, esn,
            *req->resultSetStartPoint, *req->numberOfRecordsRequested,
            error_code, addinfo,
            &number_returned,
            &next_position);
        if (error_code)
        {
            apdu_res =
                odr.create_presentResponse(apdu_req, error_code,
                                           addinfo.c_str());
        }
        else
        {
            apdu_res =
                odr.create_presentResponse(apdu_req, 0, 0);
            Z_PresentResponse *resp = apdu_res->u.presentResponse;
            resp->records = records;
            *resp->numberOfRecordsReturned = number_returned;
            *resp->nextResultSetPosition = next_position;
        }
    }
    else
    {
        apdu_res = odr.create_close(apdu_req,
                                    Z_Close_protocolError,
                                    "sparql: unhandled APDU");
        package.session().close();
    }

    assert(apdu_res);
    package.response() = apdu_res;
}

void yf::SPARQL::process(mp::Package &package) const
{
    Z_APDU *apdu;
    SessionPtr p = get_session(package, &apdu);
    if (p && apdu)
    {
        p->handle_z(package, apdu);
    }
    else
        package.move();
    release_session(package);
}

static mp::filter::Base* filter_creator()
{
    return new mp::filter::SPARQL;
}

extern "C" {
    struct metaproxy_1_filter_struct metaproxy_1_filter_sparql = {
        0,
        "sparql",
        filter_creator
    };
}


/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

