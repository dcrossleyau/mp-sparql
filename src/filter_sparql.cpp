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
            yaz_sparql_t s;
            ~Conf();
        };
        class SPARQL::Rep {
            friend class SPARQL;
            boost::condition m_cond_session_ready;
            boost::mutex m_mutex;
            std::map<mp::Session,SessionPtr> m_clients;
        };
        class SPARQL::FrontendSet {
        public:
            FrontendSet();
            ~FrontendSet();
        private:
            friend class Session;
            Odr_int hits;
            xmlDoc *doc;
        };
        class SPARQL::Session {
        public:
            Session(const SPARQL *);
            ~Session();
            void handle_z(Package &package, Z_APDU *apdu);
            Z_APDU *run_sparql(mp::Package &package,
                               Z_APDU *apdu_req,
                               mp::odr &odr,
                               const char *sparql_query,
                               const char *uri);
            bool m_in_use;
        private:
            bool m_support_named_result_sets;
            FrontendSets m_frontend_sets;
            const SPARQL *m_sparql;
        };
    }
}

yf::SPARQL::FrontendSet::~FrontendSet()
{
    if (doc)
        xmlFreeDoc(doc);
}

yf::SPARQL::FrontendSet::FrontendSet()
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

    for (; ptr; ptr = ptr->next)
    {
        if (ptr->type != XML_ELEMENT_NODE)
            continue;
        if (!strcmp((const char *) ptr->name, "db"))
        {
            yaz_sparql_t s = yaz_sparql_create();
            ConfPtr conf(new Conf);
            conf->s = s;

            const struct _xmlAttr *attr;
            for (attr = ptr->properties; attr; attr = attr->next)
            {
                if (!strcmp((const char *) attr->name, "path"))
                    conf->db = mp::xml::get_text(attr->children);
                else if (!strcmp((const char *) attr->name, "uri"))
                    conf->uri = mp::xml::get_text(attr->children);
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

static const xmlNode *get_result(xmlDoc *doc, Odr_int *sz, Odr_int pos)
{
    const xmlNode *ptr = xmlDocGetRootElement(doc);
    Odr_int cur = 0;
    for (; ptr; ptr = ptr->next)
        if (ptr->type == XML_ELEMENT_NODE &&
            !strcmp((const char *) ptr->name, "sparql"))
            break;
    if (ptr)
    {
        for (ptr = ptr->children; ptr; ptr = ptr->next)
            if (ptr->type == XML_ELEMENT_NODE &&
                !strcmp((const char *) ptr->name, "results"))
                break;
    }
    if (ptr)
    {
        for (ptr = ptr->children; ptr; ptr = ptr->next)
            if (ptr->type == XML_ELEMENT_NODE &&
                !strcmp((const char *) ptr->name, "result"))
            {
                if (cur++ == pos)
                    break;
            }
    }
    if (sz)
        *sz = cur;
    return ptr;
}

Z_APDU *yf::SPARQL::Session::run_sparql(mp::Package &package,
                                        Z_APDU *apdu_req,
                                        mp::odr &odr,
                                        const char *sparql_query,
                                        const char *uri)
{
    Package http_package(package.session(), package.origin());

    http_package.copy_filter(package);
    Z_GDU *gdu = z_get_HTTP_Request_uri(odr, uri, 0, 1);

    z_HTTP_header_add(odr, &gdu->u.HTTP_Request->headers,
                      "Content-Type", "application/x-www-form-urlencoded");
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
    Z_APDU *apdu_res = 0;
    if (gdu_resp && gdu_resp->which == Z_GDU_HTTP_Response)
    {
        Z_HTTP_Response *resp = gdu_resp->u.HTTP_Response;
        FrontendSetPtr fset(new FrontendSet);

        fset->doc = xmlParseMemory(resp->content_buf, resp->content_len);
        if (!fset->doc)
            apdu_res = odr.create_searchResponse(apdu_req,
                                             YAZ_BIB1_TEMPORARY_SYSTEM_ERROR,
                                             "invalid XML from backendbackend");
        else
        {
            apdu_res = odr.create_searchResponse(apdu_req, 0, 0);
            get_result(fset->doc, apdu_res->u.searchResponse->resultCount,
                       -1);
            m_frontend_sets[apdu_req->u.searchRequest->resultSetName] = fset;
        }
    }
    else
    {
        yaz_log(YLOG_LOG, "sparql: no HTTP response");
        apdu_res = odr.create_searchResponse(apdu_req,
                                             YAZ_BIB1_TEMPORARY_SYSTEM_ERROR,
                                             "no HTTP response from backend");
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
                package.response() = apdu_res;
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

            it = m_sparql->db_conf.begin();
            for (; it != m_sparql->db_conf.end(); it++)
                if (yaz_match_glob((*it)->db.c_str(), db.c_str()))
                    break;
            if (it == m_sparql->db_conf.end())
            {
                apdu_res = odr.create_searchResponse(
                    apdu_req, YAZ_BIB1_DATABASE_DOES_NOT_EXIST, db.c_str());
            }
            else
            {
                WRBUF addinfo_wr = wrbuf_alloc();
                WRBUF sparql_wr = wrbuf_alloc();
                int error =
                    yaz_sparql_from_rpn_wrbuf((*it)->s,
                                              addinfo_wr, sparql_wr,
                                              req->query->u.type_1);
                if (error)
                {
                    apdu_res = odr.create_searchResponse(
                        apdu_req, error,
                        wrbuf_len(addinfo_wr) ?
                        wrbuf_cstr(addinfo_wr) : 0);
                }
                else
                {
                    apdu_res = run_sparql(package, apdu_req, odr,
                                          wrbuf_cstr(sparql_wr),
                                          (*it)->uri.c_str());
                }
                wrbuf_destroy(addinfo_wr);
                wrbuf_destroy(sparql_wr);
            }
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

