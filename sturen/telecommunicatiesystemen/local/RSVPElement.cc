#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include "RSVPElement.hh"
#include <stdexcept>

CLICK_DECLS

uint16_t sizeofRSVPObject(uint8_t class_num, uint8_t c_type)
{
	size_t size;

	switch (class_num) {
	case 1:
		size = sizeof(RSVPSession);
		break;
	case 3:
		size = sizeof(RSVPHop);
		break;
	case 4:
		size = sizeof(RSVPIntegrity);
		break;
	case 5:
		size = sizeof(RSVPTimeValues);
		break;
	case 6:
		size = sizeof(RSVPErrorSpec);
		break;
	case 8:
		size = sizeof(RSVPStyle);
		break;
	case 9:
		size = sizeof(RSVPFlowspec);
		break;
	case 10:
		size = sizeof(RSVPFilterSpec);
		break;
	case 11:
		size = sizeof(RSVPSenderTemplate);
		break;
	case 12:
		size = sizeof(RSVPSenderTSpec);
		break;
	case 15:
		size = sizeof(RSVPResvConf);
		break;
	default:
		printf("sizeofRSVPClass: requesting size of undefined class num %d", class_num);
		throw std::runtime_error("sizeofRSVPClass: requesting size of undefined class num");
	}

	return size;
}

uint16_t sizeofRSVPScopeObject(size_t num_addresses) {
	return num_addresses ? (sizeof(RSVPObjectHeader) + num_addresses * sizeof(in_addr)) : 0;
}

void initRSVPCommonHeader(RSVPCommonHeader* header, uint8_t msg_type, uint8_t send_TTL, uint16_t length)
{
	header->vers = 1;
	header->flags = 0;
	header->msg_type = msg_type;
	header->send_TTL = send_TTL;
	header->RSVP_checksum = 0;
	header->reserved = 0;
	header->RSVP_length = htons(length);

	return;
}

void initRSVPObjectHeader(RSVPObjectHeader* header, uint8_t class_num, uint8_t c_type)
{
	header->length = htons(sizeofRSVPObject(class_num, c_type));
	header->class_num = class_num;
	header->c_type = c_type;

	return;
}

void initRSVPSession(RSVPSession* session, in_addr destinationAddress, uint8_t protocol_id, bool police, uint16_t dst_port)
{
	initRSVPObjectHeader(&session->header, RSVP_CLASS_SESSION, 1);

	session->IPv4_dest_address = destinationAddress;
	session->protocol_id = protocol_id;
	session->flags = 0;
	if (police) {
		session->flags |= 0x01;
	}
	session->dst_port = htons(dst_port);

	return;
}

void initRSVPHop(RSVPHop* hop, in_addr next_previous_hop_address, uint32_t logical_interface_handle)
{
	initRSVPObjectHeader(&hop->header, RSVP_CLASS_RSVP_HOP, 1);

	hop->IPv4_next_previous_hop_address = next_previous_hop_address;
	hop->logical_interface_handle = htonl(logical_interface_handle);

	return;
}

void initRSVPTimeValues(RSVPTimeValues* timeValues, uint32_t refresh_period_r)
{
	initRSVPObjectHeader(&timeValues->header, RSVP_CLASS_TIME_VALUES, 1);

	timeValues->refresh_period_r = htonl(refresh_period_r);

	return;
}

void initRSVPStyle(RSVPStyle* style)
{
	initRSVPObjectHeader(&style->header, RSVP_CLASS_STYLE, 1);

	style->flags = 0;
	style->option_vector = htons(10) << 8; // three rightmost bits: 010 for explicit sender selection, next two bits: 01 for distinct reservations

	return;
}

void initRSVPErrorSpec(RSVPErrorSpec* errorSpec, in_addr error_node_address, bool inPlace, bool notGuilty, uint8_t errorCode, uint16_t errorValue) {
	initRSVPObjectHeader(&errorSpec->header, RSVP_CLASS_ERROR_SPEC, 1);
	
	errorSpec->IPv4_error_node_address = error_node_address;
	errorSpec->flags = 0;
	
	errorSpec->flags |= (inPlace ? 0x1 : 0x0) | (notGuilty ? 0x2 : 0x0);
	errorSpec->error_code = errorCode;
	errorSpec->error_value = htons(errorValue);

	return;
}

void initRSVPResvConf(RSVPResvConf* resvConf, in_addr receiverAddress) {
	initRSVPObjectHeader(&resvConf->header, RSVP_CLASS_RESV_CONF, 1);
	
	resvConf->receiver_address = receiverAddress;
	
	return;
}

// returns pointer to the position just after the scope object
void* initRSVPScope(RSVPObjectHeader* header, const Vector<in_addr>& src_addresses)
{
	if (!src_addresses.size()) {
		return (void *) header;
	}

	header->length = htons(sizeofRSVPScopeObject(src_addresses.size()));
	header->class_num = RSVP_CLASS_SCOPE;
	header->c_type = 1;

	in_addr* address = (in_addr *) (header + 1);

	for (int i = 0; i < src_addresses.size(); ++i) {
		*address = src_addresses.at(i);
		address += 1;
	}

	return (void *) address;
}

void initRSVPFlowspec(RSVPFlowspec* flowspec,
	float token_bucket_rate,
	float token_bucket_size,
	float peak_data_rate,
	uint32_t minimum_policed_unit,
	uint32_t maximum_packet_size)
{
	initRSVPObjectHeader(&flowspec->header, RSVP_CLASS_FLOWSPEC, 2);

	flowspec->nothing_1 = 0;
	flowspec->overall_length = htons(7);
	flowspec->service_header = 1;
	flowspec->nothing_2 = 0;
	flowspec->controlled_load_data_length = htons(6);
	flowspec->parameter_id = 127;
	flowspec->flags = 0;
	flowspec->parameter_127_length = 5;
	flowspec->token_bucket_rate_float = htonl(* (uint32_t *) (&token_bucket_rate));
	flowspec->token_bucket_size_float = htonl(* (uint32_t *) (&token_bucket_size));
	flowspec->peak_data_rate_float = htonl(* (uint32_t *) (&peak_data_rate));
	flowspec->minimum_policed_unit = htonl(minimum_policed_unit);
	flowspec->maximum_packet_size = htonl(maximum_packet_size);
}

void initRSVPFilterSpec(RSVPFilterSpec* filterSpec, in_addr src_address, uint16_t src_port)
{
	initRSVPObjectHeader(&filterSpec->header, RSVP_CLASS_FILTER_SPEC, 1);

	filterSpec->src_address = src_address;
	filterSpec->nothing = 0;
	filterSpec->src_port = htons(src_port);

	return;
}

void initRSVPSenderTemplate(RSVPSenderTemplate* senderTemplate, in_addr src_address, uint16_t src_port)
{
	initRSVPFilterSpec(senderTemplate, src_address, src_port);

	senderTemplate->header.class_num = RSVP_CLASS_SENDER_TEMPLATE;
}

void initRSVPSenderTSpec(RSVPSenderTSpec* senderTSpec,
	float token_bucket_rate,
	float token_bucket_size,
	float peak_data_rate,
	uint32_t minimum_policed_unit,
	uint32_t maximum_packet_size)
{
	initRSVPObjectHeader(&senderTSpec->header, RSVP_CLASS_SENDER_TSPEC, 2);

	senderTSpec->nothing_1 = 0;
	senderTSpec->overall_length = htons(7);
	senderTSpec->service_header = 1;
	senderTSpec->nothing_2 = 0;
	senderTSpec->service_data_length = htons(6);
	senderTSpec->parameter_id = 127;
	senderTSpec->flags = 0;
	senderTSpec->parameter_127_length = 5;
	senderTSpec->token_bucket_rate_float = htonl(* (uint32_t *) (&token_bucket_rate));
	senderTSpec->token_bucket_size_float = htonl(* (uint32_t *) (&token_bucket_size));
	senderTSpec->peak_data_rate_float = htonl(* (uint32_t *) (&peak_data_rate));
	senderTSpec->minimum_policed_unit = htonl(minimum_policed_unit);
	senderTSpec->maximum_packet_size = htonl(maximum_packet_size);

	return;
}

RSVPElement::RSVPElement() : _timer(this)
{}

RSVPElement::~ RSVPElement()
{}

int RSVPElement::configure(Vector<String> &conf, ErrorHandler *errh) {
	return 0;
}

int RSVPElement::initialize(ErrorHandler* errh) {
	// _timer.initialize(this);
	
	clean();
	
	// _timer.schedule_after_msec(1000);

	return 0;
}

void RSVPElement::run_timer(Timer *) {
	clean();
	/*output(0).push(createResvMessage());
	output(0).push(createPathMessage());
	output(0).push(createPathErrMessage());
	output(0).push(createResvErrMessage());
	output(0).push(createPathTearMessage());
	output(0).push(createResvTearMessage());
	output(0).push(createResvConfMessage());*/
	
	//_timer.reschedule_after_msec(1000);
	
	return;
}

void RSVPElement::push(int, Packet *p){

}

Packet* RSVPElement::pull(int){
	return NULL;
}

int RSVPElement::sessionHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh, 
		"DEST", cpkM, cpIPAddress, &me->_session_destination_address, 
		"PROTOCOL", cpkM, cpUnsigned, &me->_session_protocol_ID,
		"POLICE", cpkM, cpBool, &me->_session_police,
		"PORT", cpkM, cpUnsigned, &me->_session_destination_port, 
		cpEnd) < 0) return -1;
		
	return 0;
}

int RSVPElement::hopHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh, 
		"NEIGHBOR", cpkM, cpIPAddress, &me->_hop_neighbor_address, 
		"LIH", cpkM, cpUnsigned, &me->_hop_logical_interface_handle,  
		cpEnd) < 0) return -1;
	
	return 0;
}

int RSVPElement::errorSpecHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;
	
	if (cp_va_kparse(conf, me, errh, 
		"ERROR_NODE_ADDRESS", cpkM, cpIPAddress, &me->_errorspec_error_node_address,
		"INPLACE", cpkM, cpBool, &me->_errorspec_inPlace,
		"NOTGUILTY", cpkM, cpBool, &me->_errorspec_notGuilty,
		"ERROR_CODE", cpkM, cpUnsigned, &me->_errorspec_errorCode,
		"ERROR_VALUE", cpkM, cpUnsigned, &me->_errorspec_errorValue,
		cpEnd) < 0) return -1;
	
	return 0;
}

int RSVPElement::timeValuesHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh, 
		"REFRESH", cpkM, cpUnsigned, &me->_timeValues_refresh_period_r, 
		cpEnd) < 0) return -1;
	
	return 0;
}

int RSVPElement::resvConfObjectHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh,
		"RECEIVER_ADDRESS", cpkM, cpIPAddress, &me->_resvConf_receiver_address,
		cpEnd) < 0) return -1;

	me->_resvConf = true;

	return 0;
}

int RSVPElement::pathHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh, "TTL", cpkM, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	Packet* message = me->createPathMessage();
	me->output(0).push(message);
	
	me->clean();
	return 0;
}

int RSVPElement::resvHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh, "TTL", cpkM, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	Packet* message = me->createResvMessage();
	me->output(0).push(message);
	
	me->clean();
	return 0;
}

int RSVPElement::pathErrHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh, "TTL", cpkM, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	Packet* message = me->createPathErrMessage();
	me->output(0).push(message);
	
	me->clean();

	return 0;
}

int RSVPElement::resvErrHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh, "TTL", cpkM, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	Packet* message = me->createResvErrMessage();
	me->output(0).push(message);
	
	me->clean();

	return 0;
}

int RSVPElement::pathTearHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh, "TTL", cpkM, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	Packet* message = me->createPathTearMessage();
	me->output(0).push(message);
	
	me->clean();

	return 0;
}

int RSVPElement::resvTearHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh, "TTL", cpkM, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	Packet* message = me->createResvTearMessage();
	me->output(0).push(message);
	
	me->clean();

	return 0;
}

int RSVPElement::resvConfHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;
	if (cp_va_kparse(conf, me, errh, "TTL", cpkM, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	Packet* message = me->createResvConfMessage();
	me->output(0).push(message);
	
	me->clean();
	return 0;
}

int RSVPElement::scopeHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh)
{
	RSVPElement *me = (RSVPElement *) e;

	in_addr src_address;

	if (cp_va_kparse(conf, me, errh, "SRC_ADDRESS", cpkM, cpIPAddress, &src_address, cpEnd) < 0) return -1;

	me->_scope_src_addresses.push_back(src_address);

	return 0;
}

int RSVPElement::senderDescriptorHandle(const String &conf, Element *e, void *thunk, ErrorHandler *errh)
{
	RSVPElement *me = (RSVPElement *) e;

	double tbr, tbs, pdr;

	if (!cp_va_kparse(conf, me, errh,
		// sender template
		"SRC_ADDRESS", cpkM, cpIPAddress, &me->_senderTemplate_src_address,
		"SRC_PORT", cpkM, cpUnsigned, &me->_senderTemplate_src_port,
		// sender tspec
		"TOKEN_BUCKET_RATE", cpkM, cpDouble, &tbr,
		"TOKEN_BUCKET_SIZE", cpkM, cpDouble, &tbs,
		"PEAK_DATA_RATE", cpkM, cpDouble, &pdr,
		"MINIMUM_POLICED_UNIT", cpkM, cpInteger, &me->_senderTSpec_minimum_policed_unit,
		"MAXIMUM_PACKET_SIZE", cpkM, cpInteger, &me->_senderTSpec_maximum_packet_size,
		cpEnd)) return -1;

	me->_senderTSpec_token_bucket_rate = tbr;
	me->_senderTSpec_token_bucket_size = tbs;
	me->_senderTSpec_peak_data_rate = pdr;

	me->_senderDescriptor = true;

	return 0;
}

String RSVPElement::getTTLHandle(Element *e, void * thunk) {
	RSVPElement *me = (RSVPElement *) e;
	return String((int) me->_TTL);
}

void RSVPElement::add_handlers() {
	// types of messages
	add_write_handler("path", &pathHandle, (void *) 0);
	add_write_handler("resv", &resvHandle, (void *) 0);
	add_write_handler("patherr", &pathErrHandle, (void *) 0);
	add_write_handler("resverr", &resvErrHandle, (void *) 0);
	add_write_handler("pathtear", &pathTearHandle, (void *) 0);
	add_write_handler("resvtear", &resvTearHandle, (void *) 0);
	add_write_handler("resvconf", &resvConfHandle, (void *) 0);
	
	// types of objects
	add_write_handler("session", &sessionHandle, (void *) 0);
	add_write_handler("hop", &hopHandle, (void *)0);
	add_write_handler("errorspec", &errorSpecHandle, (void *) 0);
	add_write_handler("timevalues", &timeValuesHandle, (void *) 0);
	add_write_handler("resvconfobj", &resvConfObjectHandle, (void *) 0);
	add_write_handler("scope", &scopeHandle, (void *) 0);
	add_write_handler("senderdescriptor", &senderDescriptorHandle, (void *) 0);

	// random read handler
	add_read_handler("TTL", &getTTLHandle, (void *) 0);
}

WritablePacket* RSVPElement::createPacket(uint16_t packetSize) const
{
	unsigned headroom = sizeof(click_ip) + sizeof(click_ether);
	unsigned tailroom = 0;

	WritablePacket* message = Packet::make(headroom, 0, packetSize, tailroom);

	if (!message) click_chatter("RSVPElement::createPathMessage: cannot make element!");

	memset(message->data(), 0, message->length());

	return message;
}

Packet* RSVPElement::createPathMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop) +
		sizeof(RSVPTimeValues) +
		(_senderDescriptor ?
			sizeof(RSVPSenderTemplate) +
			sizeof(RSVPSenderTSpec)
			: 0);
	
	WritablePacket* message = createPacket(packetSize);
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPHop* hop                   = (RSVPHop *)          (session      + 1);
	RSVPTimeValues* timeValues     = (RSVPTimeValues *)   (hop          + 1);
	RSVPSenderTemplate* senderTemplate = (RSVPSenderTemplate *) (timeValues + 1);
	RSVPSenderTSpec* senderTSpec   = (RSVPSenderTSpec *)  (senderTemplate + 1);

	initRSVPCommonHeader(commonHeader, RSVP_MSG_PATH, _TTL, packetSize);
	initRSVPSession(session, _session_destination_address, _session_protocol_ID, _session_police, _session_destination_port);
	initRSVPHop(hop, _hop_neighbor_address, _hop_logical_interface_handle);
	initRSVPTimeValues(timeValues, _timeValues_refresh_period_r);
	if (_senderDescriptor) {
		initRSVPSenderTemplate(senderTemplate, _senderTemplate_src_address, _senderTemplate_src_port);
		initRSVPSenderTSpec(senderTSpec, _senderTSpec_token_bucket_rate,
			_senderTSpec_token_bucket_size, _senderTSpec_peak_data_rate,
			_senderTSpec_minimum_policed_unit, _senderTSpec_maximum_packet_size);
	}
	
	
	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

Packet* RSVPElement::createResvMessage() const {
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop) +
		sizeof(RSVPTimeValues) +
		(_resvConf ? sizeof(RSVPResvConf) : 0) +
		sizeofRSVPScopeObject(_scope_src_addresses.size()) +
		sizeof(RSVPStyle) +
		sizeof(RSVPFlowspec);

	WritablePacket* message = createPacket(packetSize);
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPHop* hop                   = (RSVPHop *)          (session      + 1);
	RSVPTimeValues* timeValues     = (RSVPTimeValues *)   (hop          + 1);
	RSVPResvConf* resvConf         = (RSVPResvConf *)     (timeValues   + 1);
	RSVPStyle* style               = (RSVPStyle *)        initRSVPScope((RSVPObjectHeader *) (resvConf + (_resvConf ? 1 : 0)), _scope_src_addresses);
	RSVPFlowspec* flowspec         = (RSVPFlowspec *)     (style        + 1);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_RESV, _TTL, packetSize);
	initRSVPSession(session, _session_destination_address, _session_protocol_ID, _session_police, _session_destination_port);
	initRSVPHop(hop, _hop_neighbor_address, _hop_logical_interface_handle);
	initRSVPTimeValues(timeValues, _timeValues_refresh_period_r);
	if (_resvConf) initRSVPResvConf(resvConf, _resvConf_receiver_address);
	initRSVPStyle(style);
	initRSVPFlowspec(flowspec, 30.5, 0.4e38f, -5.0, 50, 100);
	
	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

Packet* RSVPElement::createPathErrMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPErrorSpec);
	unsigned tailroom = 0;
	
	WritablePacket* message = createPacket(packetSize);
	
	memset(message->data(), 0, message->length());
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPErrorSpec* errorSpec       = (RSVPErrorSpec *)    (session      + 1);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_PATHERR, _TTL, packetSize);
	initRSVPSession(session, _session_destination_address, _session_protocol_ID, _session_police, _session_destination_port);
	initRSVPErrorSpec(errorSpec, _errorspec_error_node_address, _errorspec_inPlace, _errorspec_notGuilty, _errorspec_errorCode, _errorspec_errorValue);
	
	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

Packet* RSVPElement::createResvErrMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop) +
		sizeof(RSVPErrorSpec) +
		sizeofRSVPScopeObject(_scope_src_addresses.size()) +
		sizeof(RSVPStyle);
	
	WritablePacket* message = createPacket(packetSize);
	
	memset(message->data(), 0, message->length());
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPHop* hop                   = (RSVPHop *)          (session      + 1);
	RSVPErrorSpec* errorSpec       = (RSVPErrorSpec *)    (hop          + 1);
	RSVPStyle* style               = (RSVPStyle *)        initRSVPScope((RSVPObjectHeader *) (errorSpec + 1), _scope_src_addresses);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_RESVERR, _TTL, packetSize);
	initRSVPSession(session, _session_destination_address, _session_protocol_ID, _session_police, _session_destination_port);
	initRSVPHop(hop, _hop_neighbor_address, _hop_logical_interface_handle);
	initRSVPErrorSpec(errorSpec, _errorspec_error_node_address, _errorspec_inPlace, _errorspec_notGuilty, _errorspec_errorCode, _errorspec_errorValue);
	initRSVPStyle(style);
	
	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

Packet* RSVPElement::createPathTearMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop);
	
	WritablePacket* message = createPacket(packetSize);
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPHop* hop                   = (RSVPHop *)          (session      + 1);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_PATHTEAR, _TTL, packetSize);
	initRSVPSession(session, _session_destination_address, _session_protocol_ID, _session_police, _session_destination_port);
	initRSVPHop(hop, _hop_neighbor_address, _hop_logical_interface_handle);
	
	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

Packet* RSVPElement::createResvTearMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop) +
		sizeofRSVPScopeObject(_scope_src_addresses.size()) +
		sizeof(RSVPStyle);
	
	WritablePacket* message = createPacket(packetSize);
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPHop* hop                   = (RSVPHop *)          (session      + 1);
	RSVPStyle* style               = (RSVPStyle *)        initRSVPScope((RSVPObjectHeader *) (hop + 1), _scope_src_addresses);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_RESVTEAR, _TTL, packetSize);
	initRSVPSession(session, _session_destination_address, _session_protocol_ID, _session_police, _session_destination_port);
	initRSVPHop(hop, _hop_neighbor_address, _hop_logical_interface_handle);
	initRSVPStyle(style);
	
	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

Packet* RSVPElement::createResvConfMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPErrorSpec) +
		sizeof(RSVPResvConf) +
		sizeof(RSVPStyle);
	
	WritablePacket* message = createPacket(packetSize);
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPErrorSpec* errorSpec       = (RSVPErrorSpec *)    (session      + 1);
	RSVPResvConf* resvConf         = (RSVPResvConf *)     (errorSpec    + 1);
	RSVPStyle* style               = (RSVPStyle *)        (resvConf     + 1);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_RESVCONF, _TTL, packetSize);
	initRSVPSession(session, _session_destination_address, _session_protocol_ID, _session_police, _session_destination_port);
	initRSVPErrorSpec(errorSpec, _errorspec_error_node_address, _errorspec_inPlace, _errorspec_notGuilty, _errorspec_errorCode, _errorspec_errorValue);
	initRSVPResvConf(resvConf, _resvConf_receiver_address);
	initRSVPStyle(style);
	
	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

void RSVPElement::clean() {
	_TTL = 250;
	
	_session_destination_address = IPAddress("0.0.0.0").in_addr();
	_session_protocol_ID = 0;
	_session_police = false;
	_session_destination_port = 0;
	
	_errorspec_error_node_address = IPAddress("0.0.0.0").in_addr();
	_errorspec_inPlace = false;
	_errorspec_notGuilty = false;
	_errorspec_errorCode = 0;
	_errorspec_errorValue = 0;
	
	_hop_neighbor_address = IPAddress("0.0.0.0").in_addr();
	_hop_logical_interface_handle = 0;
	
	_timeValues_refresh_period_r = 0;
	
	_flowspec = false;
	_filterspec = false;
	_senderDescriptor = false;

	_senderTemplate_src_address = IPAddress("0.0.0.0").in_addr();
	_senderTemplate_src_port = 0;

	_senderTSpec_token_bucket_rate = 0.0f;
	_senderTSpec_token_bucket_size = 0.0f;
	_senderTSpec_peak_data_rate = 0.0f;
	_senderTSpec_minimum_policed_unit = 0;
	_senderTSpec_maximum_packet_size = 0;

	_senderTSpec = false;
	_resvConf = false;
	_resvConf_receiver_address = IPAddress("0.0.0.0").in_addr();

	_scope_src_addresses.clear();
}	

CLICK_ENDDECLS
EXPORT_ELEMENT(RSVPElement)