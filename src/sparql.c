/**
 * \file sparql.c
 * \brief SPARQL
 */

#include <assert.h>
#include <yaz/diagbib1.h>
#include <yaz/tokenizer.h>
#include "sparql.h"

struct sparql_entry {
    char *pattern;
    char *value;
    struct sparql_entry *next;
};

struct yaz_sparql_s {
    NMEM nmem;
    struct sparql_entry *conf;
    struct sparql_entry **last;
};

yaz_sparql_t yaz_sparql_create(void)
{
    NMEM nmem = nmem_create();
    yaz_sparql_t s = (yaz_sparql_t) nmem_malloc(nmem, sizeof *s);

    s->nmem = nmem;
    s->conf = 0;
    s->last = &s->conf;
    return s;
}

void yaz_sparql_destroy(yaz_sparql_t s)
{
    if (s)
        nmem_destroy(s->nmem);
}

void yaz_sparql_include(yaz_sparql_t s, yaz_sparql_t u)
{
    struct sparql_entry *e = u->conf;
    for (; e; e = e->next)
        yaz_sparql_add_pattern(s, e->pattern, e->value);
}

int yaz_sparql_add_pattern(yaz_sparql_t s, const char *pattern,
                           const char *value)
{
    struct sparql_entry *e;
    assert(s);

    e = (struct sparql_entry *) nmem_malloc(s->nmem, sizeof(*e));
    e->pattern = nmem_strdup(s->nmem, pattern);
    e->value = nmem_strdup(s->nmem, value);
    e->next = 0;
    *s->last = e;
    s->last = &e->next;
    return 0;
}

int yaz_sparql_from_rpn_wrbuf(yaz_sparql_t s, WRBUF addinfo, WRBUF w,
                              Z_RPNQuery *q)
{
    return yaz_sparql_from_rpn_stream(s, addinfo, wrbuf_vp_puts, w, q);
}

int yaz_sparql_from_uri_wrbuf(yaz_sparql_t s, WRBUF addinfo, WRBUF w,
                              const char *uri, const char *schema)
{
    return yaz_sparql_from_uri_stream(s, addinfo, wrbuf_vp_puts, w, uri,
                                      schema);
}

static Odr_int lookup_attr_numeric(Z_AttributeList *attributes, int type)
{
    int j;
    for (j = 0; j < attributes->num_attributes; j++)
    {
        Z_AttributeElement *ae = attributes->attributes[j];
        if (*ae->attributeType == type)
        {
            if (ae->which == Z_AttributeValue_numeric)
                return *ae->value.numeric;
        }
    }
    return 0;
}

static const char *lookup_attr_string(Z_AttributeList *attributes, int type)
{
    int j;
    for (j = 0; j < attributes->num_attributes; j++)
    {
        Z_AttributeElement *ae = attributes->attributes[j];
        if (*ae->attributeType == type)
        {
            if (ae->which == Z_AttributeValue_complex)
            {
                Z_ComplexAttribute *ca = ae->value.complex;
                int i;
                for (i = 0; i < ca->num_list; i++)
                {
                    Z_StringOrNumeric *son = ca->list[i];
                    if (son->which == Z_StringOrNumeric_string)
                        return son->u.string;
                }
            }
        }
    }
    return 0;
}

static int z_term(yaz_sparql_t s, WRBUF addinfo, WRBUF res, WRBUF vars,
                  struct sparql_entry *e, const char *use_var,
                  Z_Term *term, int indent, int *var_no)
{
    const char *cp;
    for (cp = e->value; *cp; cp++)
    {
        if (strchr(" \t\r\n\f", *cp) && !use_var)
        {
            use_var = e->value;
            if (strchr("$?", e->value[0]))
            {
                wrbuf_write(vars, e->value + 1, cp - e->value - 1);
                wrbuf_puts(vars, " ");
            }
        }
        if (*cp == '%')
        {
            switch (*++cp)
            {
            case 's':
                wrbuf_puts(addinfo, "\"");
                switch (term->which)
                {
                case Z_Term_general:
                    wrbuf_json_write(addinfo,
                                term->u.general->buf, term->u.general->len);
                    break;
                case Z_Term_numeric:
                    wrbuf_printf(addinfo, ODR_INT_PRINTF, *term->u.numeric);
                    break;
                case Z_Term_characterString:
                    wrbuf_json_puts(addinfo, term->u.characterString);
                    break;
                }
                wrbuf_puts(addinfo, "\"");
                break;
            case 'u':
                wrbuf_puts(addinfo, "<");
                switch (term->which)
                {
                case Z_Term_general:
                    wrbuf_json_write(addinfo,
                                term->u.general->buf, term->u.general->len);
                    break;
                case Z_Term_numeric:
                    wrbuf_printf(addinfo, ODR_INT_PRINTF, *term->u.numeric);
                    break;
                case Z_Term_characterString:
                    wrbuf_json_puts(addinfo, term->u.characterString);
                    break;
                }
                wrbuf_puts(addinfo, ">");
                break;
            case 't':
                switch (term->which)
                {
                case Z_Term_general:
                    wrbuf_json_write(addinfo,
                                term->u.general->buf, term->u.general->len);
                    break;
                case Z_Term_numeric:
                    wrbuf_printf(addinfo, ODR_INT_PRINTF, *term->u.numeric);
                    break;
                case Z_Term_characterString:
                    wrbuf_json_puts(addinfo, term->u.characterString);
                    break;
                }
                break;
            case 'd':
                switch (term->which)
                {
                case Z_Term_general:
                    wrbuf_write(addinfo,
                                term->u.general->buf, term->u.general->len);
                    break;
                case Z_Term_numeric:
                    wrbuf_printf(addinfo, ODR_INT_PRINTF, *term->u.numeric);
                    break;
                case Z_Term_characterString:
                    wrbuf_puts(addinfo, term->u.characterString);
                    break;
                }
                break;
            case 'v':
                wrbuf_printf(addinfo, "?v%d", *var_no);
                break;
            case '%':
                wrbuf_putc(addinfo, '%');
                break;
            }
        }
        else
            wrbuf_putc(addinfo, *cp);
    }
    wrbuf_puts(res, wrbuf_cstr(addinfo));
    return 0;
}

static int apt(yaz_sparql_t s, WRBUF addinfo, WRBUF res, WRBUF vars,
               Z_AttributesPlusTerm *q, int indent, int *var_no)
{
    Odr_int v = lookup_attr_numeric(q->attributes, 1);
    struct sparql_entry *e = 0;
    const char *use_var = 0;
    int i;

    wrbuf_puts(res, "  ");
    for (i = 0; i < indent; i++)
        wrbuf_puts(res, " ");
    if (v)
    {
        for (e = s->conf; e; e = e->next)
        {
            if (!strncmp(e->pattern, "index.", 6))
            {
                char *end = 0;
                Odr_int w = odr_strtol(e->pattern + 6, &end, 10);

                if (end && *end == '\0' && v == w)
                    break;
            }
        }
        if (!e)
        {
            wrbuf_printf(addinfo, ODR_INT_PRINTF, v);
            return YAZ_BIB1_UNSUPP_USE_ATTRIBUTE;
        }
    }
    else
    {
        const char *index_name = lookup_attr_string(q->attributes, 1);
        if (!index_name)
            index_name = "any";
        for (e = s->conf; e; e = e->next)
        {
            if (!strncmp(e->pattern, "index.", 6))
            {
                if (!strcmp(e->pattern + 6, index_name))
                    break;
            }
        }
        if (!e)
        {
            wrbuf_puts(addinfo, index_name);
            return YAZ_BIB1_UNSUPP_USE_ATTRIBUTE;
        }
    }
    assert(e);
    wrbuf_rewind(addinfo);

    z_term(s, addinfo, res, vars, e, use_var, q->term, indent, var_no);
    (*var_no)++;
    return 0;
}


static int rpn_structure(yaz_sparql_t s, WRBUF addinfo,
                         WRBUF res, WRBUF vars, Z_RPNStructure *q, int indent,
                         int *var_no)
{
    int i;
    if (q->which == Z_RPNStructure_complex)
    {
        int r;
        Z_Complex *c = q->u.complex;
        Z_Operator *op = c->roperator;
        if (op->which == Z_Operator_and)
        {
            r = rpn_structure(s, addinfo, res, vars, c->s1, indent, var_no);
            if (r)
                return r;
            wrbuf_puts(res, " .\n");
            return rpn_structure(s, addinfo, res, vars, c->s2, indent, var_no);
        }
        else if (op->which == Z_Operator_or)
        {
            for (i = 0; i < indent; i++)
                wrbuf_puts(res, " ");
            wrbuf_puts(res, "  {\n");
            r = rpn_structure(s, addinfo, res, vars, c->s1, indent + 1, var_no);
            if (r)
                return r;
            wrbuf_puts(res, "\n");
            for (i = 0; i < indent; i++)
                wrbuf_puts(res, " ");
            wrbuf_puts(res, "  } UNION {\n");
            r = rpn_structure(s, addinfo, res, vars, c->s2, indent + 1, var_no);
            wrbuf_puts(res, "\n");
            for (i = 0; i < indent; i++)
                wrbuf_puts(res, " ");
            wrbuf_puts(res, "  }");
            return r;
        }
        else
        {
            return YAZ_BIB1_OPERATOR_UNSUPP;
        }
    }
    else
    {
        Z_Operand *op = q->u.simple;
        if (op->which == Z_Operand_APT)
            return apt(s, addinfo, res, vars, op->u.attributesPlusTerm, indent,
                       var_no);
        else
            return YAZ_BIB1_RESULT_SET_UNSUPP_AS_A_SEARCH_TERM;
    }
    return 0;
}

static int emit_prefixes(yaz_sparql_t s,
                          WRBUF addinfo,
                          void (*pr)(const char *buf,
                                     void *client_data),
                          void *client_data)
{
    struct sparql_entry *e;
    yaz_tok_cfg_t cfg = yaz_tok_cfg_create();
    int errors = 0;
    for (e = s->conf; e; e = e->next)
    {
        if (!strcmp(e->pattern, "prefix"))
        {
            yaz_tok_parse_t p = yaz_tok_parse_buf(cfg, e->value);
            int no = 0;

            pr("PREFIX", client_data);
            while (1)
            {
                const char *tok_str;
                int token = yaz_tok_move(p);
                if (token != YAZ_TOK_STRING)
                    break;
                pr(" ", client_data);

                tok_str = yaz_tok_parse_string(p);
                if (tok_str[0])
                {
                    if (no > 0 && tok_str[0] != '<')
                        pr("<", client_data);
                    pr(tok_str, client_data);
                    if (no > 0 && tok_str[strlen(tok_str)-1] != '>')
                        pr(">", client_data);
                }
                no++;
            }
            pr("\n", client_data);
            yaz_tok_parse_destroy(p);
        }
        else if (!strcmp(e->pattern, "criteria"))
        {
            ;
        }
        else if (!strcmp(e->pattern, "criteria.optional"))
        {
            ;
        }
        else if (!strncmp(e->pattern, "index.", 6))
        {
            ;
        }
        else if (!strcmp(e->pattern, "form"))
        {
            ;
        }
        else if (!strcmp(e->pattern, "modifier"))
        {
            ;
        }
        else if (!strncmp(e->pattern, "present", 7))
        {
            ;
        }
        else if (!strncmp(e->pattern, "uri", 3))
        {
            ;
        }
        else
        {
            errors++;
        }
    }
    yaz_tok_cfg_destroy(cfg);
    return errors;
}

struct sparql_entry *lookup_schema(yaz_sparql_t s, const char *schema)
{
    struct sparql_entry *e;

    for (e = s->conf; e; e = e->next)
    {
        if (!strncmp(e->pattern, "present.", 8))
        {
            if (!schema || !strcmp(e->pattern + 8, schema))
                break;
        }
        if (!strncmp(e->pattern, "uri.", 4))
        {
            if (!schema || !strcmp(e->pattern + 4, schema))
                break;
        }
    }
    return e;
}

int yaz_sparql_lookup_schema(yaz_sparql_t s, const char *schema)
{
    return lookup_schema(s, schema) ? 1 : 0;
}

int yaz_sparql_from_uri_stream(yaz_sparql_t s,
                               WRBUF addinfo,
                               void (*pr)(const char *buf, void *client_data),
                               void *client_data,
                               const char *uri, const char *schema)
{
    int r = 0, errors = emit_prefixes(s, addinfo, pr, client_data);
    struct sparql_entry *e = lookup_schema(s, schema);
    if (!e)
        errors++;
    if (!errors)
    {
        WRBUF res = wrbuf_alloc();
        WRBUF vars = wrbuf_alloc();
        int var_no = 0;
        Z_Term term;

        term.which = Z_Term_characterString;
        term.u.characterString = (char *) uri;
        r = z_term(s, addinfo, res, vars, e, 0, &term, 0, &var_no);
        if (!r)
        {
            pr(wrbuf_cstr(res), client_data);
            pr("\n", client_data);
        }
        wrbuf_destroy(res);
        wrbuf_destroy(vars);
    }
    return errors ? -1 : r;
}

int yaz_sparql_from_rpn_stream(yaz_sparql_t s,
                               WRBUF addinfo,
                               void (*pr)(const char *buf,
                                          void *client_data),
                               void *client_data,
                               Z_RPNQuery *q)
{
    int r = 0, errors = emit_prefixes(s, addinfo, pr, client_data);
    struct sparql_entry *e;

    for (e = s->conf; e; e = e->next)
    {
        if (!strcmp(e->pattern, "form"))
        {
            pr(e->value, client_data);
            pr("\n", client_data);
        }
    }
    pr("WHERE {\n", client_data);
    for (e = s->conf; e; e = e->next)
    {
        if (!strcmp(e->pattern, "criteria"))
        {
            pr("  ", client_data);
            pr(e->value, client_data);
            pr(" .\n", client_data);
        }
    }
    if (!errors)
    {
        WRBUF res = wrbuf_alloc();
        WRBUF vars = wrbuf_alloc();
        int var_no = 0;
        r = rpn_structure(s, addinfo, res, vars, q->RPNStructure, 0, &var_no);
        if (r == 0)
        {
            WRBUF t_var = wrbuf_alloc();
            for (e = s->conf; e; e = e->next)
            {
                if (!strcmp(e->pattern, "criteria.optional"))
                {
                    int optional = 1;
                    size_t i = strlen(e->value), j;

                    while (i > 0 && strchr(" \t\r\n\f", e->value[i-1]))
                        --i;
                    j = i;
                    while (i > 0 && !strchr("$?", e->value[i-1]))
                        --i;
                    if (i > 0 && j > i)
                    {
                        wrbuf_rewind(t_var);
                        wrbuf_write(t_var, e->value + i, j - i);
                        wrbuf_puts(t_var, " ");
                        if (strstr(wrbuf_cstr(vars), wrbuf_cstr(t_var)))
                            optional = 0;
                    }

                    pr("  ", client_data);
                    if (optional)
                        pr("OPTIONAL { ", client_data);
                    pr(e->value, client_data);
                    if (optional)
                        pr(" }", client_data);
                    pr(" .\n", client_data);
                }
            }
            pr(wrbuf_cstr(res), client_data);
            wrbuf_destroy(t_var);
        }
        wrbuf_destroy(res);
        wrbuf_destroy(vars);
    }
    pr("\n}\n", client_data);

    for (e = s->conf; e; e = e->next)
    {
        if (!strcmp(e->pattern, "modifier"))
        {
            pr(e->value, client_data);
            pr("\n", client_data);
        }
    }
    return errors ? -1 : r;
}

/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

