/*
  eXosip - This is the eXtended osip library.
  Copyright (C) 2002, 2003  Aymeric MOIZARD  - jack@atosc.org
  
  eXosip is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2 of the License, or
  (at your option) any later version.
  
  eXosip is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.
  
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef ENABLE_MPATROL
#include <mpatrol.h>
#endif

#include <eXosip.h>

extern char *localip;
extern char *localport;


char *register_callid_number = NULL;

/* should use cryptographically random identifier is RECOMMENDED.... */
/* by now this should lead to identical call-id when application are
   started at the same time...   */
char *
osip_call_id_new_random()
{
  char *tmp = (char *)osip_malloc(33);
  unsigned int number = osip_build_random_number();
  sprintf(tmp,"%u",number);
  return tmp;
}

char *
osip_from_tag_new_random()
{
  return osip_call_id_new_random();
}

char *
osip_to_tag_new_random()
{
  return osip_call_id_new_random();
}

unsigned int
via_branch_new_random()
{
  return osip_build_random_number();
}

/* prepare a minimal request (outside of a dialog) with required headers */
/* 
   method_name is the type of request. ("INVITE", "REGISTER"...)
   to is the remote target URI
   transport is either "TCP" or "UDP" (by now, only UDP is implemented!)
*/
int
generating_request_out_of_dialog(osip_message_t **dest, char *method_name,
				 char *to, char *transport, char *from,
				 char *proxy)
{
  /* Section 8.1:
     A valid request contains at a minimum "To, From, Call-iD, Cseq,
     Max-Forwards and Via
  */
  static int register_osip_cseq_number = 1; /* always start registration with 1 */
  int i;
  osip_message_t *request;

  if (register_callid_number==NULL)
    register_callid_number = osip_call_id_new_random();

  i = msg_init(&request);
  if (i!=0) return -1;

  /* prepare the request-line */
  osip_message_set_method(request, osip_strdup(method_name));
  osip_message_set_version(request, osip_strdup("SIP/2.0"));
  osip_message_set_statuscode(request, NULL);
  osip_message_set_reasonphrase(request, NULL);

  if (0==strcmp("REGISTER", method_name))
    {
      osip_uri_init(&(request->rquri));
      i = osip_uri_parse(request->rquri, proxy);
      if (i!=0)
	{
	  osip_uri_free(request->rquri);
	  goto brood_error_1;
	}
      osip_parser_set_to(request, from);
    }
  else
    {
      /* in any cases except REGISTER: */
      i = osip_parser_set_to(request, to);
      if (i!=0)
	{
	  fprintf(stderr, "ERROR: callee address does not seems to be a sipurl: %s\n", to);
	  goto brood_error_1;
	}
      if (proxy!=NULL)
	{  /* equal to a pre-existing route set */
	   /* if the pre-existing route set contains a "lr" (compliance
	      with bis-08) then the rquri should contains the remote target
	      URI */
	  osip_uri_param_t *lr_param;
	  osip_route_t *o_proxy;
#ifndef __VXWORKS_OS__
	  osip_route_init(&o_proxy);
#else
	  osip_route_init2(&o_proxy);
#endif
	  i = osip_route_parse(o_proxy, proxy);
	  if (i!=0) {
	    osip_route_free(o_proxy);
	    goto brood_error_1;
	  }

	  osip_uri_uparam_get_byname(o_proxy->url, "lr", &lr_param);
	  if (lr_param!=NULL) /* to is the remote target URI in this case! */
	    {
	      osip_uri_clone(request->to->url, &(request->rquri));
	      /* "[request] MUST includes a Route header field containing
	       the route set values in order." */
	      osip_list_add(request->routes, o_proxy, 0);
	    }
	  else
	    /* if the first URI of route set does not contain "lr", the rquri
	       is set to the first uri of route set */
	    {
	      osip_uri_uparam_get_byname(o_proxy->url, "lr", &lr_param);
	      request->rquri = o_proxy->url;
	      o_proxy->url = NULL;
	      osip_route_free(o_proxy);
	      /* add the route set */
	      /* "The UAC MUST add a route header field containing
		 the remainder of the route set values in order.
		 The UAC MUST then place the remote target URI into
		 the route header field as the last value
	       */
	      osip_parser_set_route(request, to);
	    }
	}
      else /* No route set (outbound proxy) is used */
	{
	  /* The UAC must put the remote target URI (to field) in the rquri */
	    i = osip_uri_clone(request->to->url, &(request->rquri));
	    if (i!=0) goto brood_error_1;
	}
    }

  /* set To and From */
  osip_parser_set_from(request, from);
  /* add a tag */
  osip_from_set_tag(request->from, osip_from_tag_new_random());
  
  /* set the cseq and call_id header */
  if (0==strcmp("REGISTER", method_name))
    {
      osip_call_id_t *callid;
      osip_cseq_t *cseq;
      char *num;

      /* call-id is always the same for REGISTRATIONS */
      i = osip_call_id_init(&callid);
      if (i!=0) goto brood_error_1;
      osip_call_id_set_number(callid, osip_strdup(register_callid_number));
      osip_call_id_set_host(callid, osip_strdup(localip));
      request->call_id = callid;

      i = osip_cseq_init(&cseq);
      if (i!=0) goto brood_error_1;
      num = (char *)osip_malloc(20); /* should never be more than 10 chars... */
      sprintf(num, "%i", register_osip_cseq_number);
      osip_cseq_set_number(cseq, num);
      osip_cseq_set_method(cseq, osip_strdup(method_name));
      request->cseq = cseq;

      register_osip_cseq_number++;
    }
  else
    {
      /* set the call-id */
      osip_call_id_t *callid;
      osip_cseq_t *cseq;
      i = osip_call_id_init(&callid);
      if (i!=0) goto brood_error_1;
      osip_call_id_set_number(callid, osip_call_id_new_random());
      osip_call_id_set_host(callid, osip_strdup(localip));
      request->call_id = callid;

      i = osip_cseq_init(&cseq);
      if (i!=0) goto brood_error_1;
      osip_cseq_set_number(cseq, osip_strdup("20")); /* always start with 20... :-> */
      osip_cseq_set_method(cseq, osip_strdup(method_name));
      request->cseq = cseq;
    }

  /* always add the Max-Forward header */
  osip_parser_set_max_forwards(request, "5"); /* a UA should start a request with 70 */

  {
    char *tmp = (char *)osip_malloc(90*sizeof(char));
    sprintf(tmp, "SIP/2.0/%s %s:%s;branch=z9hG4bK%u", transport,
	    localip,
	    localport,
	    via_branch_new_random() );
    osip_parser_set_via(request, tmp);
    osip_free(tmp);
  }

  /* add specific headers for each kind of request... */

  if (0==strcmp("INVITE", method_name) || 0==strcmp("SUBSCRIBE", method_name))
    {
      char *contact;
      osip_from_t *a_from;
      int i;
      i = osip_from_init(&a_from);
      if (i==0)
	i = osip_from_parse(a_from, from);

      if (i==0 && a_from!=NULL
	  && a_from->url!=NULL && a_from->url->username!=NULL )
	{
	  contact = (char *) osip_malloc(50+strlen(a_from->url->username));
	  if (localport==NULL)
	    sprintf(contact, "<sip:%s@%s>", a_from->url->username,
		    localip);
	  else
	    sprintf(contact, "<sip:%s@%s:%s>", a_from->url->username,
		    localip,
		    localport);
	  
	  osip_parser_set_contact(request, contact);
	  osip_free(contact);
	}
      osip_from_free(a_from);

      /* This is probably useless for other messages */
      osip_parser_set_allow(request, "INVITE");
      osip_parser_set_allow(request, "ACK");
      osip_parser_set_allow(request, "CANCEL");
      osip_parser_set_allow(request, "BYE");
      osip_parser_set_allow(request, "OPTIONS");
      osip_parser_set_allow(request, "REFER");
      osip_parser_set_allow(request, "SUBSCRIBE");
      osip_parser_set_allow(request, "NOTIFY");
      osip_parser_set_allow(request, "MESSAGE");
    }

  if (0==strcmp("SUBSCRIBE", method_name))
    {
      osip_parser_set_header(request, "Event", "presence");
#ifdef SUPPORT_MSN
      osip_parser_set_accept(request, "application/xpidf+xml");
#else
      osip_parser_set_accept(request, "application/cpim-pidf+xml");
#endif
    }
  else if (0==strcmp("REGISTER", method_name))
    {
    }
  else if (0==strcmp("INFO", method_name))
    {

    }
  else if (0==strcmp("OPTIONS", method_name))
    {

    }

  osip_parser_set_user_agent(request, "josua/0.6.2");
  /*  else if ... */
  *dest = request;
  return 0;

 brood_error_1:
  msg_free(request);
  *dest = NULL;
  return -1;
}

int
generating_register(osip_message_t **reg, char *from,
		    char *proxy, char *contact)
{
  osip_from_t *a_from;
  int i;
  i = generating_request_out_of_dialog(reg, "REGISTER", NULL, "UDP",
				       from, proxy);
  if (i!=0) return -1;


  if (contact==NULL)
    {
      contact = (char *) osip_malloc(50);
      i = osip_from_init(&a_from);
      if (i==0)
	i = osip_from_parse(a_from, from);

      if (i==0 && a_from!=NULL
	  && a_from->url!=NULL && a_from->url->username!=NULL )
	{
	  contact = (char *) osip_malloc(50+strlen(a_from->url->username));
	  if (localport==NULL)
	    sprintf(contact, "<sip:%s@%s>", a_from->url->username,
		    localip);
	  else
	    sprintf(contact, "<sip:%s@%s:%s>", a_from->url->username,
		    localip,
		    localport);
	  
	  osip_parser_set_contact(*reg, contact);
	  osip_free(contact);
	}
      osip_from_free(a_from);
    }
  else
    {
      osip_parser_set_contact(*reg, contact);
    }
  osip_parser_set_header(*reg, "expires", "3600");
  osip_parser_set_content_length(*reg, "0");
  
  return 0;
}

/* this method can't be called unless the previous
   INVITE transaction is over. */
int eXosip_build_initial_invite(osip_message_t **invite, char *to, char *from,
				char *route, char *subject)
{
  int i;

  if (to!=NULL && *to=='\0')
    return -1;

  osip_clrspace(to);
  osip_clrspace(subject);
  osip_clrspace(from);
  osip_clrspace(route);
  if (route!=NULL && *route=='\0')
    route=NULL;
  if (subject!=NULL && *subject=='\0')
    subject=NULL;

  i = generating_request_out_of_dialog(invite, "INVITE", to, "UDP", from,
				       route);
  if (i!=0) return -1;
  
  osip_parser_set_subject(*invite, subject);

  /* after this delay, we should send a CANCEL */
  osip_parser_set_expires(*invite, "120");

  /* osip_parser_set_organization(*invite, "Jack's Org"); */
  return 0;
}

/* this method can't be called unless the previous
   INVITE transaction is over. */
int generating_initial_subscribe(osip_message_t **subscribe, char *to,
				 char *from, char *route)
{
  int i;

  if (to!=NULL && *to=='\0')
    return -1;

  osip_clrspace(to);
  osip_clrspace(from);
  osip_clrspace(route);
  if (route!=NULL && *route=='\0')
    route=NULL;

  i = generating_request_out_of_dialog(subscribe, "SUBSCRIBE", to, "UDP", from,
				       route);
  if (i!=0) return -1;
  
#define LOW_EXPIRE
#ifdef LOW_EXPIRE
  osip_parser_set_expires(*subscribe, "60");
#else
  osip_parser_set_expires(*subscribe, "600");
#endif

  /* osip_parser_set_organization(*subscribe, "Jack's Org"); */
  return 0;
}

/* this method can't be called unless the previous
   INVITE transaction is over. */
int generating_message(osip_message_t **message, char *to, char *from,
		       char *route, char *buff)
{
  int i;

  if (to!=NULL && *to=='\0')
    return -1;

  osip_clrspace(to);
  /*  osip_clrspace(buff); */
  osip_clrspace(from);
  osip_clrspace(route);
  if (route!=NULL && *route=='\0')
    route=NULL;
  if (buff!=NULL && *buff=='\0')
    return -1; /* at least, the message must be of length >= 1 */
  
  i = generating_request_out_of_dialog(message, "MESSAGE", to, "UDP", from,
				       route);
  if (i!=0) return -1;
  
  /* after this delay, we should send a CANCEL */
  osip_parser_set_expires(*message, "120");

  osip_parser_set_body(*message, buff);
  osip_parser_set_content_type(*message, "xxxx/yyyy");

  /* osip_parser_set_organization(*message, "Jack's Org"); */


  return 0;
}


int
generating_options(osip_message_t **options, char *from, char *to, char *sdp, char *proxy)
{
  int i;
  i = generating_request_out_of_dialog(options, "OPTIONS", to, "UDP",
				       from, proxy);
  if (i!=0) return -1;

  if (sdp!=NULL)
    {      
      osip_parser_set_content_type(*options, "application/sdp");
      osip_parser_set_body(*options, sdp);
    }
  return 0;
}


int
dialog_fill_route_set(osip_dialog_t *dialog, osip_message_t *request)
{
  /* if the pre-existing route set contains a "lr" (compliance
     with bis-08) then the rquri should contains the remote target
     URI */
  int i;
  int pos=0;
  osip_uri_param_t *lr_param;
  osip_route_t *route;
  char *last_route;
  /* AMD bug: fixed 17/06/2002 */

  if (dialog->type==CALLER)
    {
      pos = osip_list_size(dialog->route_set)-1;
      route = (osip_route_t*)osip_list_get(dialog->route_set, pos);
    }
  else
    route = (osip_route_t*)osip_list_get(dialog->route_set, 0);
    
  osip_uri_uparam_get_byname(route->url, "lr", &lr_param);
  if (lr_param!=NULL) /* the remote target URI is the rquri! */
    {
      i = osip_uri_clone(dialog->remote_contact_uri->url,
		    &(request->rquri));
      if (i!=0) return -1;
      /* "[request] MUST includes a Route header field containing
	 the route set values in order." */
      /* AMD bug: fixed 17/06/2002 */
      pos=0; /* first element is at index 0 */
      while (!osip_list_eol(dialog->route_set, pos))
	{
	  osip_route_t *route2;
	  route = osip_list_get(dialog->route_set, pos);
	  i = osip_route_clone(route, &route2);
	  if (i!=0) return -1;
	  if (dialog->type==CALLER)
	    osip_list_add(request->routes, route2, 0);
	  else
	    osip_list_add(request->routes, route2, -1);
	  pos++;
	}
      return 0;
    }

  /* if the first URI of route set does not contain "lr", the rquri
     is set to the first uri of route set */
  
  
  i = osip_uri_clone(route->url, &(request->rquri));
  if (i!=0) return -1;
  /* add the route set */
  /* "The UAC MUST add a route header field containing
     the remainder of the route set values in order. */
  pos=0; /* yes it is */
  
  while (!osip_list_eol(dialog->route_set, pos)) /* not the first one in the list */
    {
      osip_route_t *route2;
      route = osip_list_get(dialog->route_set, pos);
      i = osip_route_clone(route, &route2);
      if (i!=0) return -1;
      if (dialog->type==CALLER)
	{
	  if (pos!=0)
	    osip_list_add(request->routes, route2, 0);
	}
      else
	{
	  if (!osip_list_eol(dialog->route_set, pos+1))
	    osip_list_add(request->routes, route2, -1);
	}
	  pos++;
    }
      /* The UAC MUST then place the remote target URI into
	 the route header field as the last value */
  i = osip_uri_to_str(dialog->remote_contact_uri->url, &last_route);
  if (i!=0) return -1;
  i = osip_parser_set_route(request, last_route);
  if (i!=0) { osip_free(last_route); return -1; }

  
  /* route header and rquri set */
  return 0;
}

int
_eXosip_build_request_within_dialog(osip_message_t **dest, char *method_name,
				   osip_dialog_t *dialog, char *transport)
{
  int i;
  osip_message_t *request;

  i = msg_init(&request);
  if (i!=0) return -1;

  if (dialog->remote_contact_uri==NULL)
    {
      /* this dialog is probably not established! or the remote UA
	 is not compliant with the latest RFC
      */
      msg_free(request);
      return -1;
    }
  /* prepare the request-line */
  request->sipmethod  = osip_strdup(method_name);
  request->sipversion = osip_strdup("SIP/2.0");
  request->statuscode   = NULL;
  request->reasonphrase = NULL;

  /* and the request uri???? */
  if (osip_list_eol(dialog->route_set, 0))
    {
      /* The UAC must put the remote target URI (to field) in the rquri */
      i = osip_uri_clone(dialog->remote_contact_uri->url, &(request->rquri));
      if (i!=0) goto grwd_error_1;
    }
  else
    {
      /* fill the request-uri, and the route headers. */
      dialog_fill_route_set(dialog, request);
    }
  
  /* To and From already contains the proper tag! */
  i = osip_to_clone(dialog->remote_uri, &(request->to));
  if (i!=0) goto grwd_error_1;
  i = osip_from_clone(dialog->local_uri, &(request->from));
  if (i!=0) goto grwd_error_1;

  /* set the cseq and call_id header */
  osip_parser_set_call_id(request, dialog->call_id);

  if (0==strcmp("ACK", method_name))
    {
      osip_cseq_t *cseq;
      char *tmp;
      i = osip_cseq_init(&cseq);
      if (i!=0) goto grwd_error_1;
      tmp = osip_malloc(20);
      sprintf(tmp,"%i", dialog->local_cseq);
      osip_cseq_set_number(cseq, tmp);
      osip_cseq_set_method(cseq, osip_strdup(method_name));
      request->cseq = cseq;
    }
  else
    {
      osip_cseq_t *cseq;
      char *tmp;
      i = osip_cseq_init(&cseq);
      if (i!=0) goto grwd_error_1;
      dialog->local_cseq++; /* we should we do that?? */
      tmp = osip_malloc(20);
      sprintf(tmp,"%i", dialog->local_cseq);
      osip_cseq_set_number(cseq, tmp);
      osip_cseq_set_method(cseq, osip_strdup(method_name));
      request->cseq = cseq;
    }
  
  /* always add the Max-Forward header */
  osip_parser_set_max_forwards(request, "5"); /* a UA should start a request with 70 */


  /* even for ACK for 2xx (ACK within a dialog), the branch ID MUST
     be a new ONE! */
  {
    char *tmp = (char *)osip_malloc(90*sizeof(char));
    sprintf(tmp, "SIP/2.0/%s %s:%s;branch=z9hG4bK%u", transport,
	    localip ,localport,
	    via_branch_new_random());
    osip_parser_set_via(request, tmp);
    osip_free(tmp);
  }

  /* add specific headers for each kind of request... */

  if (0==strcmp("INVITE", method_name) || 0==strcmp("SUBSCRIBE", method_name))
    {
      /* add a Contact header for requests that establish a dialog:
	 (only "INVITE") */
      /* this Contact is the global location where to send request
	 outside of a dialog! like sip:jack@atosc.org? */
      char *contact;
      contact = (char *) osip_malloc(50);
      sprintf(contact, "<sip:%s@%s:%s>", dialog->local_uri->url->username,
	      localip,
	      localport);
      osip_parser_set_contact(request, contact);
      osip_free(contact);
      /* Here we'll add the supported header if it's needed! */
      /* the require header must be added by the upper layer if needed */
    }

  if (0==strcmp("SUBSCRIBE", method_name))
    {
      osip_parser_set_header(request, "Event", "presence");
      osip_parser_set_accept(request, "application/cpim-pidf+xml");
    }
  else if (0==strcmp("NOTIFY", method_name))
    {
      osip_parser_set_content_type(request, "application/cpim-pidf+xml");
    }
  else if (0==strcmp("INFO", method_name))
    {

    }
  else if (0==strcmp("OPTIONS", method_name))
    {
      osip_parser_set_accept(request, "application/sdp");
    }
  else if (0==strcmp("ACK", method_name))
    {
      /* The ACK MUST contains the same credential than the INVITE!! */
      /* TODO... */
    }

  osip_parser_set_user_agent(request, "josua/0.6.2");
  /*  else if ... */
  *dest = request;
  return 0;

  /* grwd_error_2: */
  dialog->local_cseq--;
 grwd_error_1:
  msg_free(request);
  *dest = NULL;
  return -1;
}

/* this request is only build within a dialog!! */
int
generating_bye(osip_message_t **bye, osip_dialog_t *dialog)
{
  int i;
  i = _eXosip_build_request_within_dialog(bye, "BYE", dialog, "UDP");
  if (i!=0) return -1;

  return 0;
}

/* this request is only build within a dialog! (but should not!) */
int
generating_refer(osip_message_t **refer, osip_dialog_t *dialog, char *refer_to)
{
  int i;
  i = _eXosip_build_request_within_dialog(refer, "REFER", dialog, "UDP");
  if (i!=0) return -1;

  osip_parser_set_header(*refer, "Refer-to", refer_to);

  return 0;
}

/* this request can be inside or outside a dialog */
int
generating_options_within_dialog(osip_message_t **options, osip_dialog_t *dialog, char *sdp)
{
  int i;
  i = _eXosip_build_request_within_dialog(options, "OPTIONS", dialog, "UDP");
  if (i!=0) return -1;

  if (sdp!=NULL)
    {      
      osip_parser_set_content_type(*options, "application/sdp");
      osip_parser_set_body(*options, sdp);
    }

  return 0;
}

int
generating_info(osip_message_t **info, osip_dialog_t *dialog)
{
  int i;
  i = _eXosip_build_request_within_dialog(info, "INFO", dialog, "UDP");
  if (i!=0) return -1;
  return 0;
}

/* It is RECOMMENDED to only cancel INVITE request */
int
generating_cancel(osip_message_t **dest, osip_message_t *request_cancelled)
{
  int i;
  osip_message_t *request;
  
  i = msg_init(&request);
  if (i!=0) return -1;
  
  /* prepare the request-line */
  osip_message_set_method(request, osip_strdup("CANCEL"));
  osip_message_set_version(request, osip_strdup("SIP/2.0"));
  osip_message_set_statuscode(request, NULL);
  osip_message_set_reasonphrase(request, NULL);

  i = osip_uri_clone(request_cancelled->rquri, &(request->rquri));
  if (i!=0) goto gc_error_1;
  
  i = osip_to_clone(request_cancelled->to, &(request->to));
  if (i!=0) goto gc_error_1;
  i = osip_from_clone(request_cancelled->from, &(request->from));
  if (i!=0) goto gc_error_1;
  
  /* set the cseq and call_id header */
  i = osip_call_id_clone(request_cancelled->call_id, &(request->call_id));
  if (i!=0) goto gc_error_1;
  i = osip_cseq_clone(request_cancelled->cseq, &(request->cseq));
  if (i!=0) goto gc_error_1;
  osip_free(request->cseq->method);
  request->cseq->method = osip_strdup("CANCEL");
  
  /* copy ONLY the top most Via Field (this method is also used by proxy) */
  {
    osip_via_t *via;
    osip_via_t *via2;
    i = osip_parser_get_via(request_cancelled, 0, &via);
    if (i!=0) goto gc_error_1;
    i = osip_via_clone(via, &via2);
    if (i!=0) goto gc_error_1;
    osip_list_add(request->vias, via2, -1);
  }

  /* add the same route-set than in the previous request */
  {
    int pos=0;
    osip_route_t *route;
    osip_route_t *route2;
    while (!osip_list_eol(request_cancelled->routes, pos))
      {
	route = (osip_route_t*) osip_list_get(request_cancelled->routes, pos);
	i = osip_route_clone(route, &route2);
	if (i!=0) goto gc_error_1;
	osip_list_add(request->routes, route2, -1);
	pos++;
      }
  }

  osip_parser_set_max_forwards(request, "70"); /* a UA should start a request with 70 */
  osip_parser_set_user_agent(request, "josua/0.6.2");

  *dest = request;
  return 0;

 gc_error_1:
  msg_free(request);
  *dest = NULL;
  return -1;
}


int
generating_ack_for_2xx(osip_message_t **ack, osip_dialog_t *dialog)
{
  int i;
  i = _eXosip_build_request_within_dialog(ack, "ACK", dialog, "UDP");
  if (i!=0) return -1;

  return 0;
}
