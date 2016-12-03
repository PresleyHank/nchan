#include <nchan_module.h>

typedef struct {
  ngx_str_t            subtype;
  nchan_content_type_t ct;
} nchan_content_subtype_t;

nchan_content_type_t nchan_channel_info_type(ngx_str_t *accept) {
  nchan_content_subtype_t subtypes[] = {
    { ngx_string("plain"),    NCHAN_CONTENT_TYPE_PLAIN },
    { ngx_string("json"),     NCHAN_CONTENT_TYPE_JSON },
    { ngx_string("x-json"),   NCHAN_CONTENT_TYPE_JSON },
    { ngx_string("yaml"),     NCHAN_CONTENT_TYPE_YAML },
    { ngx_string("x-yaml"),   NCHAN_CONTENT_TYPE_YAML },
    { ngx_string("xml"),      NCHAN_CONTENT_TYPE_XML }
  };
  
  ngx_str_t  text_ = ngx_string("text/");
  ngx_str_t  application_ = ngx_string("application/");
  
  if(!accept) {
    return NCHAN_CONTENT_TYPE_PLAIN;
  }
  
  unsigned i;
  size_t len;
  u_char *cur, *last, *curend;
  
  for(cur = accept->data, last = accept->data + accept->len; cur < last; cur = curend) {
    if((curend = memchr(cur, ',', last - cur)) != NULL) {
      curend++;
    }
    else {
      curend = last;
    }
    
    if(nchan_strscanstr(&cur, &text_, curend) || nchan_strscanstr(&cur, &application_, curend)) {
      len = curend - cur;
      for(i=0; i<(sizeof(subtypes)/sizeof(nchan_content_subtype_t)); i++) {
        if(len >= subtypes[i].subtype.len && ngx_memcmp(cur, subtypes[i].subtype.data, subtypes[i].subtype.len) == 0) {
          return subtypes[i].ct;
        }
      }
    }
    else {
      continue;
    }
  }
  
  return NCHAN_CONTENT_TYPE_PLAIN;
}

ngx_buf_t                       channel_info_buf;
u_char                          channel_info_buf_str[512]; //big enough
ngx_str_t                       channel_info_content_type;

ngx_buf_t *nchan_channel_info_buf(ngx_str_t *accept_header, ngx_uint_t messages, ngx_uint_t subscribers, time_t last_seen, nchan_msg_id_t *last_msgid, ngx_str_t **generated_content_type) {
  ngx_buf_t                      *b = &channel_info_buf;
  ngx_uint_t                      len;
  const ngx_str_t                *format;
  time_t                          time_elapsed = ngx_time() - last_seen;
  static nchan_msg_id_t           zero_msgid = NCHAN_ZERO_MSGID;
  nchan_content_type_t            ct;
  
  static struct {
    ngx_str_t        content_type;
    const ngx_str_t *format_string;
  } content_type_map[] = {
    [NCHAN_CONTENT_TYPE_PLAIN]    = { ngx_string("text/plain"), &NCHAN_CHANNEL_INFO_PLAIN }, 
    [NCHAN_CONTENT_TYPE_JSON]     = { ngx_string("text/json"), &NCHAN_CHANNEL_INFO_JSON },
    [NCHAN_CONTENT_TYPE_YAML]     = { ngx_string("text/yaml"), &NCHAN_CHANNEL_INFO_YAML },
    [NCHAN_CONTENT_TYPE_XML]      = { ngx_string("text/xml"), &NCHAN_CHANNEL_INFO_XML }
  };
  
  if(!last_msgid) {
    last_msgid = &zero_msgid;
  }
 
  b->start = channel_info_buf_str;
  b->pos = b->start;
  b->last_buf = 1;
  b->last_in_chain = 1;
  b->flush = 1;
  b->memory = 1;
  
  ct = nchan_channel_info_type(accept_header);

  if(generated_content_type) {
    *generated_content_type = &content_type_map[ct].content_type;
  }
  
  format = content_type_map[ct].format_string;
  
  len = format->len - 8 - 1 + 3*NGX_INT_T_LEN; //minus 8 sprintf
  
  if(len > 512) {
    nchan_log_error("NCHAN: Channel info string too long: max: 512, is: %i", len);
  }
  
  b->last = ngx_snprintf(b->start, 512, (char *)format->data, messages, last_seen==0 ? -1 : (ngx_int_t) time_elapsed, subscribers, msgid_to_str(last_msgid));
  b->end = b->last;
  
  return b;
}

//print information about a channel
ngx_int_t nchan_channel_info(ngx_http_request_t *r, ngx_uint_t messages, ngx_uint_t subscribers, time_t last_seen, nchan_msg_id_t *msgid) {
  ngx_buf_t                      *b;
  ngx_str_t                      *content_type;
  ngx_str_t                      *accept_header = nchan_get_accept_header_value(r);

  b = nchan_channel_info_buf(accept_header, messages, subscribers, last_seen, msgid, &content_type);
  
  return nchan_respond_membuf(r, NGX_HTTP_OK, content_type, b, 0);
}

ngx_int_t nchan_response_channel_ptr_info(nchan_channel_t *channel, ngx_http_request_t *r, ngx_int_t status_code) {
  static const ngx_str_t CREATED_LINE = ngx_string("201 Created");
  static const ngx_str_t ACCEPTED_LINE = ngx_string("202 Accepted");
  
  time_t             last_seen = 0;
  ngx_uint_t         subscribers = 0;
  ngx_uint_t         messages = 0;
  nchan_msg_id_t    *msgid = NULL;
  if(channel!=NULL) {
    subscribers = channel->subscribers;
    last_seen = channel->last_seen;
    messages  = channel->messages;
    msgid = &channel->last_published_msg_id;
    r->headers_out.status = status_code == (ngx_int_t) NULL ? NGX_HTTP_OK : status_code;
    if (status_code == NGX_HTTP_CREATED) {
      ngx_memcpy(&r->headers_out.status_line, &CREATED_LINE, sizeof(ngx_str_t));
    }
    else if (status_code == NGX_HTTP_ACCEPTED) {
      ngx_memcpy(&r->headers_out.status_line, &ACCEPTED_LINE, sizeof(ngx_str_t));
    }
    return nchan_channel_info(r, messages, subscribers, last_seen, msgid);
  }
  else {
    //404!
    return nchan_respond_status(r, NGX_HTTP_NOT_FOUND, NULL, 0);
  }
}
