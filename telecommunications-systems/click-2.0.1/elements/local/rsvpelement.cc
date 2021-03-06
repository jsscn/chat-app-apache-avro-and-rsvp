#include <click/config.h>
#include <click/confparse.hh>
#include <click/error.hh>
#include <click/router.hh>
#include "rsvpelement.hh"
#include <stdexcept>
#include <ctime>

CLICK_DECLS

const void* RSVPObjectOfType(Packet* packet, uint8_t wanted_class_num) {
	uint8_t class_num;
	
	const RSVPObjectHeader* p = (const RSVPObjectHeader *) (packet->transport_header() ? packet->transport_header() : ((const unsigned char*) packet->data()) + sizeof(click_ip));
	
	p = (const RSVPObjectHeader*) (((const RSVPCommonHeader*) p) + 1);
	
	while (p < (const RSVPObjectHeader*) packet->end_data()) {
		readRSVPObjectHeader(p, &class_num, NULL);
		if (class_num == wanted_class_num) {
			return p;
		}
		
		p = nextRSVPObject(p);
	}
	
	return NULL;
}


void RSVPElement::push(int, Packet *packet) {
	if (_dead) {
		packet->kill();
		return;
	}

	// chop off IP header
	packet->pull(sizeof(click_ip));
	
	uint8_t msg_type;
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader*) packet->data();
	readRSVPCommonHeader(commonHeader, &msg_type, NULL, NULL);
	
	if (click_in_cksum((unsigned char *) commonHeader, htons(commonHeader->RSVP_length))) {
		click_chatter("%s: RSVPElement::push: received packet with bad checksum", _name.c_str());
	}
	
	bool patherr = false;
	
	RSVPSession* session = (RSVPSession *) RSVPObjectOfType(packet, RSVP_CLASS_SESSION);
	RSVPNodeSession nodeSession = *session;
	RSVPFilterSpec* filterSpec;
	RSVPFlowspec* flowspec;
	RSVPResvConf* resvConf;
	const RSVPErrorSpec* errorSpec;
	
	RSVPPathState pathState;

	WritablePacket* reply;

	switch (msg_type) {
		case RSVP_MSG_PATH:
			clean();
			
			// if auto reservation is on, respond to the path message with a reservation message
			if (find(_pathStates, nodeSession) == _pathStates.end()) {
				click_chatter("%s: creating new path state for %s", _name.c_str(), IPAddress(nodeSession._dst_ip_address).unparse().c_str());

				if (_autoResv) {
					reply = replyToPathMessage(packet->clone());
					output(0).push(reply);
				}
				
				packet->kill();
				
			} else {
				// click_chatter("Updating path state for %s at %s", IPAddress(nodeSession._dst_ip_address).unparse().c_str(), _name.c_str());
			}

			updatePathState(packet->clone());

			break;
		case RSVP_MSG_RESV:
		
			// get the necessary information in order to update the reservation
			filterSpec = (RSVPFilterSpec *) RSVPObjectOfType(packet, RSVP_CLASS_FILTER_SPEC);
			flowspec = (RSVPFlowspec *) RSVPObjectOfType(packet, RSVP_CLASS_FLOWSPEC);
			uint32_t refresh_period_r;
			readRSVPTimeValues((RSVPTimeValues *) RSVPObjectOfType(packet, RSVP_CLASS_TIME_VALUES), &refresh_period_r);
		
			if (!this->resvState(nodeSession, *filterSpec)) {
				click_chatter("%s: creating new resv state for %s", _name.c_str(), IPAddress(nodeSession._dst_ip_address).unparse().c_str(), _name.c_str());
			} else {
				// click_chatter("%s: updating resv state for %s", _name.c_str(), IPAddress(nodeSession._dst_ip_address).unparse().c_str());
			}

			updateReservation(*session, filterSpec, flowspec, refresh_period_r);
			
			if ((resvConf = (RSVPResvConf *) RSVPObjectOfType(packet, RSVP_CLASS_RESV_CONF))) {
				_session = *session;
				initRSVPErrorSpec(&_errorSpec, _myIP, false, false, 0, 0);
				_resvConf = *resvConf;
				_resvConf_given = true;
				
				WritablePacket* response = createResvConfMessage();
				addIPHeader(response, resvConf->receiver_address, _myIP, _tos);
				click_chatter("%s: responding to resv conf request", _name.c_str());
				output(0).push(response);
			}
			
			break;
		case RSVP_MSG_PATHERR:
			patherr = true;
		case RSVP_MSG_RESVERR:
			errorSpec = (const RSVPErrorSpec*) RSVPObjectOfType(packet, RSVP_CLASS_ERROR_SPEC);
			
			in_addr error_node_addr;
			uint8_t error_code;
			uint16_t error_value;
	
			readRSVPErrorSpec(errorSpec, &error_node_addr, NULL, NULL, &error_code, &error_value);
		
			click_chatter("%s: received %s error message for session %s/%d/%d, error node address %s, error code %d, error value %d",
				_name.c_str(),
				String(patherr ? "path" : "resv").c_str(),
				IPAddress(nodeSession._dst_ip_address).unparse().c_str(),
				nodeSession._protocol_id, nodeSession._dst_port,
				IPAddress(error_node_addr).unparse().c_str(),
				error_code, error_value);
			
			packet->kill();
			break;
		case RSVP_MSG_PATHTEAR:
			// restore chopped-off IP header
			packet = packet->push(sizeof(click_ip));
			RSVPNode::push(0, packet);
			break;
		case RSVP_MSG_RESVTEAR:
			packet = packet->push(sizeof(click_ip));
			RSVPNode::push(0, packet);
			break;
		case RSVP_MSG_RESVCONF:
			resvConf = (RSVPResvConf *) RSVPObjectOfType(packet, RSVP_CLASS_RESV_CONF);
			if (IPAddress(resvConf->receiver_address) == _myIP) {
				const RSVPErrorSpec* errorSpec = (const RSVPErrorSpec *) RSVPObjectOfType(packet, RSVP_CLASS_ERROR_SPEC);
				in_addr src;
				readRSVPErrorSpec(errorSpec, &src, NULL, NULL, NULL, NULL);
				click_chatter("%s: Received resvconf message from %s", _name.c_str(), IPAddress(src).unparse().c_str());
			}
			
			break;
		default:
			click_chatter("RSVPElement %s: no message type %d", _name.c_str(), msg_type);
	}
}

Packet* RSVPElement::pull(int){
	return NULL;
}

WritablePacket* RSVPElement::replyToPathMessage(Packet* pathMessage) {
	// copy session
	_session = * (RSVPSession *) RSVPObjectOfType(pathMessage, RSVP_CLASS_SESSION);
	createSession(_session); // creates a session if it doesn't exist already
	// copy time values (for now)
	_timeValues = * (RSVPTimeValues *) RSVPObjectOfType(pathMessage, RSVP_CLASS_TIME_VALUES);

	// hop address will be the resv messages's destination
	RSVPHop* pathHop = (RSVPHop *) RSVPObjectOfType(pathMessage, RSVP_CLASS_RSVP_HOP);
	in_addr resvDestinationAddress; uint32_t lih;
	readRSVPHop(pathHop, &resvDestinationAddress, &lih);

	// resv's hop address is the host's own IP
	initRSVPHop(&_hop, _myIP, lih);

	RSVPSenderTemplate* senderTemplate;
	RSVPSenderTSpec* senderTSpec;
	if ((senderTemplate = (RSVPSenderTemplate *) RSVPObjectOfType(pathMessage, RSVP_CLASS_SENDER_TEMPLATE))
			&& (senderTSpec = (RSVPSenderTSpec *) RSVPObjectOfType(pathMessage, RSVP_CLASS_SENDER_TSPEC))) {
		_flowDescriptor = true;

		// reserve the capacity the sender provides
		_filterSpec = *senderTemplate;
		initRSVPObjectHeader(&_filterSpec.header, RSVP_CLASS_FILTER_SPEC, 1);
		initRSVPFlowspec(&_flowspec, senderTSpec);
	}

	WritablePacket* resvMessage = createResvMessage();

	addIPHeader(resvMessage, resvDestinationAddress, _myIP, _tos);

	uint32_t refresh_period;
	readRSVPTimeValues(&_timeValues, &refresh_period);

	RSVPResvState resvState;
	resvState.filterSpec = _filterSpec;
	resvState.flowspec = _flowspec;
	resvState.refresh_period_r = refresh_period;

	resvState.timer = new Timer(this);

	_reservations.find(_session)->second.set(*senderTemplate, resvState);

	resvState.timer->initialize(this);
	resvState.timer->schedule_now();

	return resvMessage;
}

RSVPElement::RSVPElement()
{}

RSVPElement::~ RSVPElement()
{}

int RSVPElement::configure(Vector<String> &conf, ErrorHandler *errh) {
	_application = false;

	_autoResv = false;

	if (cp_va_kparse(conf, this, errh,
		"IP", cpkM + cpkP, cpIPAddress, &_myIP,
		"APPLICATION", 0, cpBool, &_application,
		"AUTORESV", 0, cpBool, &_autoResv, cpEnd) < 0) return -1;

	return 0;
}

int RSVPElement::initialize(ErrorHandler* errh) {
	_name = this->name();
	
	initRSVPHop(&_hop, _myIP, sizeof(RSVPHop));
	initRSVPTimeValues(&_timeValues, 5000);

	srand(time(NULL));

	_tos = 0;
	clean();

	return 0;
}

void RSVPElement::run_timer(Timer* timer) {
	// click_chatter("running timer %p", (void*) timer);
	clean();
	// figure out which session the timer belongs to
	const RSVPNodeSession* session;
	const RSVPSender* sender;

	const RSVPPathState* pathState;
	const RSVPResvState* resvState;

	unsigned refresh_period_r;
// static int i = 0;
	if ((session = sessionForSenderTimer(timer, &sender))) {
		sendPeriodicPathMessage(session, sender);
// click_chatter("%s: timer %p is for sending a periocic path message", _name.c_str(), timer);
		refresh_period_r = _senders.find(*session)->second.find(*sender)->second.refresh_period_r;
	} else if ((session = (RSVPNodeSession *) sessionForReservationTimer(timer, &sender))) {
		// click_chatter("%s: sending periodic resv message %d", _name.c_str(), ++i);
		sendPeriodicResvMessage(session, sender);
// click_chatter("%s: timer %p is for sending a periocic resv message", _name.c_str(), timer);
		refresh_period_r = _reservations.find(*session)->second.find(*sender)->second.refresh_period_r;
	} else {
		// click_chatter("%s: timer %p seems to be a timeout timer", _name.c_str(), timer);
		RSVPNode::run_timer(timer);
		return;
	}
	
	double randomized_refresh = ((double) rand() / RAND_MAX + 0.5) * refresh_period_r;
	// click_chatter("%s: setting randomized refresh to %f with R = %d", _name.c_str(), randomized_refresh / 1000, refresh_period_r);
	// click_chatter("%s: rescheduling timer %p", _name.c_str(), (void*) timer);
	timer->reschedule_after_msec(randomized_refresh);

	return;
}

void RSVPElement::sendPeriodicPathMessage(const RSVPNodeSession* session, const RSVPSender* sender) {
	HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPPathState> >::const_iterator it1 = _senders.find(*session);
	HashTable<RSVPSender, RSVPPathState>::const_iterator it2 = find(it1->second, *sender);
	RSVPPathState pathState = it2->second;

	RSVPSession packetSession;
	initRSVPSession(&packetSession, session);
	RSVPHop hop;
	initRSVPHop(&hop, _myIP, 0);
	RSVPTimeValues timeValues;
	initRSVPTimeValues(&timeValues, pathState.refresh_period_r);

	WritablePacket* message = createPathMessage(&packetSession,
		&hop, &timeValues, &pathState.senderTemplate, &pathState.senderTSpec);
	addIPHeader(message, session->_dst_ip_address, _myIP, (uint8_t) _tos);
	output(0).push(message);
}

void RSVPElement::createSession(const RSVPNodeSession& session) {
	RSVPNode::createSession(session);
	if (_senders.find(session) == _senders.end()) {
		_senders.set(session, HashTable<RSVPSender, RSVPPathState>());
	}
	if (_reservations.find(session) == _reservations.end()) {
		_reservations.set(session, HashTable<RSVPSender, RSVPResvState>());
	}
}

void RSVPElement::erasePathState(const RSVPNodeSession& session, const RSVPSender& sender) {
	RSVPNode::erasePathState(session, sender);
}

void RSVPElement::eraseResvState(const RSVPNodeSession& session, const RSVPSender& sender) {
	RSVPNode::eraseResvState(session, sender);

	HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPResvState> >::iterator resvit1 = _reservations.find(session);
	if (resvit1 == _reservations.end() && _reservations.begin() != _reservations.end() && _reservations.begin()->first == session) {
		resvit1 = _reservations.begin();
	} else if (resvit1 == _reservations.end()) {
		return;
	}

	HashTable<RSVPSender, RSVPResvState>::iterator resvit = find(resvit1->second, sender);
	
	if (resvit == resvit1->second.end() && resvit1->second.begin() != resvit1->second.end() && resvit1->second.begin()->first == sender) {
		resvit = resvit1->second.begin();
	} else if (resvit == resvit1->second.end()) {
		if (resvit1->second.begin() == resvit1->second.end()) {
		}
		return;
	}
	
	if (resvit != resvit1->second.end()) {
		if (resvit->second.timer) {
			resvit->second.timer->unschedule();
		}
		resvit1->second.erase(resvit);
	}
click_chatter("found sender and erased resv state");
	if (resvit1->second.begin() == resvit1->second.end()) {
		_reservations.erase(session);
	}
}

const RSVPPathState* RSVPElement::pathState(const RSVPNodeSession& session, const RSVPSender& sender) const {
	HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPPathState> >::const_iterator it1 = _senders.find(session);
	if (it1 == _senders.end()) {
		return RSVPNode::pathState(session, sender);
	}

	HashTable<RSVPSender, RSVPPathState>::const_iterator it = it1->second.find(sender);
	if (it == it1->second.end()) {
		return RSVPNode::pathState(session, sender);
	}

	return &it->second;
}

const RSVPResvState* RSVPElement::resvState(const RSVPNodeSession& session, const RSVPSender& sender) const {
	HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPResvState> >::const_iterator it1 = _reservations.find(session);
	if (it1 == _reservations.end()) {
		return RSVPNode::resvState(session, sender);
	}

	HashTable<RSVPSender, RSVPResvState>::const_iterator it = it1->second.find(sender);
	if (it == it1->second.end()) {
		return RSVPNode::resvState(session, sender);
	}

	return &it->second;
}

bool RSVPElement::hasReservation(const RSVPNodeSession& session, const RSVPSender& sender) const {
	if (RSVPNode::hasReservation(session, sender)) {
		return true;
	}

	return resvState(session, sender);
}

void RSVPElement::removeSender(const RSVPNodeSession& session, const RSVPSender& sender) {
	HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPPathState> >::iterator pathit1 = _senders.find(session);
	if (pathit1 == _pathStates.end()) {
		return;
	}

	HashTable<RSVPSender, RSVPPathState>::iterator pathit = pathit1->second.find(sender);
	if (pathit != pathit1->second.end()) {
		if (pathit->second.timer) {
			pathit->second.timer->unschedule();
		}
		pathit1->second.erase(pathit);
	}
	
	if (pathit1->second.begin() == pathit1->second.end()) {
		_senders.erase(session);
	}
	
	eraseResvState(session, sender);
}

void RSVPElement::removeReservation(const RSVPNodeSession& session, const RSVPSender& sender) {
	HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPResvState> >::iterator resvit1 = _reservations.find(session);
	if (resvit1 == _reservations.end()) {
		return;
	}

	HashTable<RSVPSender, RSVPResvState>::iterator resvit = resvit1->second.find(sender);
	if (resvit != resvit1->second.end()) {
		if (resvit->second.timer) {
			resvit->second.timer->unschedule();
		}
		resvit1->second.erase(resvit);
	}
	
	if (resvit1->second.begin() == resvit1->second.end()) {
		_reservations.erase(session);
	}
}

void RSVPElement::removeAllState() {
	RSVPNode::removeAllState();
	
	for (HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPPathState> >::iterator it1 = _senders.begin(); it1 != _senders.end(); it1++) {
		HashTable<RSVPSender, RSVPPathState>& senders = it1->second;
		while (senders.begin() != senders.end()) {
			RSVPPathState& pathState = senders.begin()->second;
			if (pathState.timer) {
				pathState.timer->unschedule();
			}
			senders.erase(senders.begin());
		}
	}
	
	_senders.clear();
	
	for (HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPResvState> >::iterator it1 = _reservations.begin(); it1 != _reservations.end(); it1++) {
		HashTable<RSVPSender, RSVPResvState>& reservations = it1->second;
		while (reservations.begin() != reservations.end()) {
			RSVPResvState& resvState = reservations.begin()->second;
			if (resvState.timer) {
				resvState.timer->unschedule();
			}
			reservations.erase(reservations.begin());
		}
	}
	
	_reservations.clear();
}

void RSVPElement::sendPeriodicResvMessage(const RSVPNodeSession* session, const RSVPSender* sender) {
	HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPResvState> >::iterator it1 = find(_reservations, *session);
	HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPPathState> >::const_iterator pathit1 = find(_pathStates, *session);

	if (pathit1 == _pathStates.end() || it1 == _resvStates.end()) {
		click_chatter("%s: Trying to send resv message for nonexistent session", _name.c_str());
		return;
	}

	HashTable<RSVPSender, RSVPResvState>& reservations = it1->second;
	const HashTable<RSVPSender, RSVPPathState>& pathStates = pathit1->second;

	HashTable<RSVPSender, RSVPResvState>::iterator it = find(reservations, *sender);
	HashTable<RSVPSender, RSVPPathState>::const_iterator pathit = find(pathStates, *sender);

	if (it == reservations.end()) {
		click_chatter("%s: Trying to send resv message for nonexistent reservation.", _name.c_str());
		return;
	}

	if (pathit == pathStates.end()) {
		click_chatter("%s: Trying to send resv message for nonexitent path state, looking for %s/%d", _name.c_str(), IPAddress(session->_dst_ip_address).s().c_str(), session->_dst_port);
		return;
	}

	RSVPResvState& resvState = it->second;
	const RSVPPathState& pathState = pathit->second;

	RSVPSession packetSession; initRSVPSession(&packetSession, session);
	RSVPHop hop; initRSVPHop(&hop, _myIP, 0);
	RSVPTimeValues timeValues; initRSVPTimeValues(&timeValues, resvState.refresh_period_r);

	RSVPResvConf resvConf;

	if (resvState.confirm) {
		click_chatter("%s: confirm was requested", _name.c_str());
		initRSVPResvConf(&resvConf, _myIP);
	}

	WritablePacket* message = createResvMessage(&packetSession,
		&hop,
		&timeValues,
		resvState.confirm ? &resvConf : NULL,
		&resvState.flowspec,
		&resvState.filterSpec);

	resvState.confirm = false;

	addIPHeader(message, pathState.previous_hop_node, _myIP, _tos);
	output(0).push(message);
}

const RSVPNodeSession* RSVPElement::sessionForSenderTimer(const Timer* timer, const RSVPSender** sender) const {
	for (HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPPathState> >::const_iterator it = _senders.begin(); it != _senders.end(); it++) {
		for (HashTable<RSVPSender, RSVPPathState>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
			if (it2->second.timer == timer) {
				*sender = &it2->first;
				return &it->first;
			}
		}
	}

	*sender = NULL;
	return NULL;
}

const RSVPNodeSession* RSVPElement::sessionForReservationTimer(const Timer* timer, const RSVPSender** sender) const {
	for (HashTable<RSVPNodeSession, HashTable<RSVPSender, RSVPResvState> >::const_iterator it = _reservations.begin(); it != _reservations.end(); it++) {
		for (HashTable<RSVPSender, RSVPResvState>::const_iterator it2 = it->second.begin(); it2 != it->second.end(); it2++) {
			if (it2->second.timer == timer) {
				*sender = &it2->first;
				return &it->first;
			}
		}
	}

	*sender = NULL;
	return NULL;
}

int RSVPElement::sessionHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	int protocol_ID;
	bool police = false;
	int destination_port;
	
	in_addr destination_address;
	
	if (cp_va_kparse(conf, me, errh, 
		"DEST", cpkM, cpIPAddress, &destination_address, 
		"PROTOCOL", cpkM, cpUnsigned, &protocol_ID,
		"POLICE", 0, cpBool, &police,
		"PORT", cpkM, cpUnsigned, &destination_port, 
		cpEnd) < 0) return -1;
	
	initRSVPSession(&me->_session, destination_address, (uint8_t) protocol_ID, police, (uint16_t) destination_port);

	return 0;
}

int RSVPElement::hopHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	in_addr neighbor_address;
	int logical_interface_handle;

	if (cp_va_kparse(conf, me, errh, 
		"NEIGHBOR", cpkM, cpIPAddress, &neighbor_address, 
		"LIH", cpkM, cpUnsigned, &logical_interface_handle,  
		cpEnd) < 0) return -1;

	initRSVPHop(&me->_hop, neighbor_address, (uint32_t) logical_interface_handle);

	return 0;
}

int RSVPElement::errorSpecHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;
	
	in_addr error_node_address;
	bool inPlace;
	bool notGuilty;
	int errorCode;
	int errorValue;

	if (cp_va_kparse(conf, me, errh, 
		"ERROR_NODE_ADDRESS", cpkM, cpIPAddress, &error_node_address,
		"INPLACE", cpkM, cpBool, &inPlace,
		"NOTGUILTY", cpkM, cpBool, &notGuilty,
		"ERROR_CODE", cpkM, cpUnsigned, &errorCode,
		"ERROR_VALUE", cpkM, cpUnsigned, &errorValue,
		cpEnd) < 0) return -1;
	
	initRSVPErrorSpec(&me->_errorSpec, error_node_address, inPlace, notGuilty, (uint8_t) errorCode, (uint16_t) errorValue);

	return 0;
}

int RSVPElement::timeValuesHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	int refresh_period_r;

	if (cp_va_kparse(conf, me, errh, 
		"REFRESH", cpkM, cpUnsigned, &refresh_period_r, 
		cpEnd) < 0) return -1;
	
	initRSVPTimeValues(&me->_timeValues, (uint32_t) refresh_period_r);

	return 0;
}

int RSVPElement::resvConfObjectHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	in_addr receiver_address;

	if (cp_va_kparse(conf, me, errh,
		"RECEIVER_ADDRESS", cpkM, cpIPAddress, &receiver_address,
		cpEnd) < 0) return -1;

	initRSVPResvConf(&me->_resvConf, receiver_address);

	me->_resvConf_given = true;

	return 0;
}

int RSVPElement::pathHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;
	bool refresh = true;

	//click_chatter("pathHandle: destinationIP: %s", IPAddress(destinationIP).unparse().c_str());
	if (cp_va_kparse(conf, me, errh,
		"TTL", 0, cpInteger, &me->_TTL,
		"REFRESH", 0, cpBool, &refresh, cpEnd) < 0) return -1;
	
	RSVPNodeSession nodeSession(me->_session);
	RSVPPathState pathState;
	pathState.senderTemplate = me->_senderTemplate;
	pathState.senderTSpec = me->_senderTSpec;
	readRSVPTimeValues(&me->_timeValues, &pathState.refresh_period_r);
	pathState.timer = new Timer(me);
	pathState.timer->initialize(me);
	RSVPSender sender(me->_senderTemplate);

	me->createSession(nodeSession);
	me->_senders.find(nodeSession)->second.set(sender, pathState);
	
	if (refresh) {
		pathState.timer->schedule_now();
	} else {
		me->sendPeriodicPathMessage(&nodeSession, &sender);
		me->erasePathState(nodeSession, sender);
	}
	
	me->clean();
	return 0;
}

int RSVPElement::resvHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	bool refresh = true, confirm = false;

	if (cp_va_kparse(conf, me, errh,
		"TTL", 0, cpInteger, &me->_TTL,
		"REFRESH", 0, cpBool, &refresh,
		"CONFIRM", 0, cpBool, &confirm, cpEnd) < 0) return -1;
	
	RSVPNodeSession session(me->_session);
	RSVPSender sender(me->_filterSpec);
	RSVPResvState resvState;
	
	initRSVPHop(&me->_hop, me->_myIP, 0);
	
	uint32_t refresh_period_r;
	readRSVPTimeValues(&me->_timeValues, &refresh_period_r);
	resvState.filterSpec = me->_filterSpec;
	resvState.flowspec = me->_flowspec;
	resvState.refresh_period_r = refresh_period_r;
	resvState.confirm = confirm;
	resvState.timer = new Timer(me);
	resvState.timer->initialize(me);
	
	me->createSession(session);
	
	if (find(find(me->_pathStates, session)->second, sender) == me->_pathStates.find(session)->second.end()) {
		errh->error("Did not find path state corresponding to the session %s/%d/%d,\n sender %s/%d,\n sender tspec %s",
			IPAddress(session._dst_ip_address).unparse().c_str(),
			session._protocol_id,
			session._dst_port,
			IPAddress(sender.src_address).unparse().c_str(),
			sender.src_port,
			me->specToString(resvState.flowspec).c_str());
		return -1;
	}
	
	me->_reservations.find(session)->second.set(sender, resvState);
	
	if (refresh) {
		resvState.timer->schedule_now();
	} else {
		me->sendPeriodicResvMessage(&session, &sender);
		me->eraseResvState(session, sender);
	}
	
	me->clean();
	return 0;
}

int RSVPElement::pathErrHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh,
		"TTL", 0, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	WritablePacket* message = me->createPathErrMessage();
	me->addIPHeader(message, me->_session.IPv4_dest_address, me->_myIP, (uint8_t) me->_tos);
	me->output(0).push(message);
	
	me->clean();

	return 0;
}

int RSVPElement::resvErrHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh,
		"TTL", 0, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	RSVPNodeSession session = me->_session;
	RSVPSender sender = me->_filterSpec;
	
	me->createSession(session);
	
	HashTable<RSVPSender, RSVPPathState>::const_iterator pathit = find(find(me->_pathStates, session)->second, sender);
	
	if (pathit == me->_pathStates.find(me->_session)->second.end()) {
		errh->error("Did not find path state corresponding to the session %s/%d/%d,\n sender %s/%d,\n sender tspec %s",
			IPAddress(session._dst_ip_address).unparse().c_str(),
			session._protocol_id,
			session._dst_port,
			IPAddress(sender.src_address).unparse().c_str(),
			sender.src_port,
			me->specToString(me->_flowspec).c_str());
		return -1;
	}
	
	RSVPPathState pathState = pathit->second;
	
	WritablePacket* message = me->createResvErrMessage();
	me->addIPHeader(message, pathState.previous_hop_node, me->_myIP, (uint8_t) me->_tos);
	me->output(0).push(message);
	
	me->clean();

	return 0;
}

int RSVPElement::pathTearHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh,
		"TTL", 0, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	me->removeSender(me->_session, me->_senderTemplate);

	WritablePacket* message = me->createPathTearMessage();
	me->addIPHeader(message, me->_session.IPv4_dest_address, me->_myIP, (uint8_t) me->_tos);
	me->output(0).push(message);
	
	me->clean();

	return 0;
}

int RSVPElement::resvTearHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh,
		"TTL", 0, cpInteger, &me->_TTL, cpEnd) < 0) return -1;

	const RSVPPathState* pathState = me->pathState(me->_session, me->_filterSpec);
	
	if (!pathState) {
		errh->error("Trying to tear down resv state with no matching path state.");
		return -1;
	}
	
	me->removeReservation(me->_session, me->_filterSpec);
	
	WritablePacket* message = me->createResvTearMessage();
	me->addIPHeader(message, pathState->previous_hop_node, me->_myIP, (uint8_t) me->_tos);
	me->output(0).push(message);
	
	me->clean();

	return 0;
}

int RSVPElement::resvConfHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement * me = (RSVPElement *) e;

	if (cp_va_kparse(conf, me, errh,
		"TTL", 0, cpInteger, &me->_TTL, cpEnd) < 0) return -1;
	
	WritablePacket* message = me->createResvConfMessage();
	me->addIPHeader(message, me->_session.IPv4_dest_address, me->_myIP, (uint8_t) me->_tos);
	me->output(0).push(message);
	
	me->clean();
	return 0;
}

int RSVPElement::tosHandle(const String &conf, Element *e, void * thunk, ErrorHandler *errh) {
	RSVPElement* me = (RSVPElement *) e;
	
	cp_va_kparse(conf, e, errh, "TOS", cpkP + cpkM, cpInteger, &me->_tos, cpEnd);

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

	in_addr senderTemplate_src_address;
	unsigned senderTemplate_src_port;

	float senderTSpec_token_bucket_rate;
	float senderTSpec_token_bucket_size;
	float senderTSpec_peak_data_rate;
	int senderTSpec_minimum_policed_unit;
	int senderTSpec_maximum_packet_size;

	if (!cp_va_kparse(conf, me, errh,
		// sender template
		"SRC_ADDRESS", cpkM, cpIPAddress, &senderTemplate_src_address,
		"SRC_PORT", cpkM, cpUnsigned, &senderTemplate_src_port,
		// sender tspec
		"TOKEN_BUCKET_RATE", cpkM, cpDouble, &tbr,
		"TOKEN_BUCKET_SIZE", cpkM, cpDouble, &tbs,
		"PEAK_DATA_RATE", cpkM, cpDouble, &pdr,
		"MINIMUM_POLICED_UNIT", cpkM, cpInteger, &senderTSpec_minimum_policed_unit,
		"MAXIMUM_PACKET_SIZE", cpkM, cpInteger, &senderTSpec_maximum_packet_size,
		cpEnd)) return -1;

	senderTSpec_token_bucket_rate = tbr;
	senderTSpec_token_bucket_size = tbs;
	senderTSpec_peak_data_rate = pdr;

	initRSVPSenderTemplate(&me->_senderTemplate, senderTemplate_src_address, (uint16_t) senderTemplate_src_port);
	initRSVPSenderTSpec(&me->_senderTSpec, senderTSpec_token_bucket_rate,
		senderTSpec_token_bucket_size, senderTSpec_peak_data_rate,
		(uint32_t) senderTSpec_minimum_policed_unit, (uint32_t) senderTSpec_maximum_packet_size);

	me->_senderDescriptor = true;

	return 0;
}

int RSVPElement::flowDescriptorHandle(const String& conf, Element *e, void *thunk, ErrorHandler *errh)
{
	RSVPElement* me = (RSVPElement *) e;

	double tbr, tbs, pdr;

	in_addr filterSpec_src_address;
	unsigned filterSpec_src_port;

	float flowspec_token_bucket_rate;
	float flowspec_token_bucket_size;
	float flowspec_peak_data_rate;
	int flowspec_minimum_policed_unit;
	int flowspec_maximum_packet_size;

	if (!cp_va_kparse(conf, me, errh,
		// filter spec
		"SRC_ADDRESS", cpkM, cpIPAddress, &filterSpec_src_address,
		"SRC_PORT", cpkM, cpUnsigned, &filterSpec_src_port,
		// flowspec
		"TOKEN_BUCKET_RATE", cpkM, cpDouble, &tbr,
		"TOKEN_BUCKET_SIZE", cpkM, cpDouble, &tbs,
		"PEAK_DATA_RATE", cpkM, cpDouble, &pdr,
		"MINIMUM_POLICED_UNIT", cpkM, cpInteger, &flowspec_minimum_policed_unit,
		"MAXIMUM_PACKET_SIZE", cpkM, cpInteger, &flowspec_maximum_packet_size,
		cpEnd)) return -1;

	flowspec_token_bucket_rate = tbr;
	flowspec_token_bucket_size = tbs;
	flowspec_peak_data_rate = pdr;

	initRSVPFilterSpec(&me->_filterSpec, filterSpec_src_address, (uint16_t) filterSpec_src_port);
	initRSVPFlowspec(&me->_flowspec, flowspec_token_bucket_rate,
		flowspec_token_bucket_size, flowspec_peak_data_rate,
		(uint32_t) flowspec_minimum_policed_unit, (uint32_t) flowspec_maximum_packet_size);

	me->_flowDescriptor = true;

	return 0;
}

String RSVPElement::getTTLHandle(Element *e, void * thunk) {
	RSVPElement *me = (RSVPElement *) e;
	return String((int) me->_TTL);
}

String RSVPElement::sendersTableHandle(Element *e, void *thunk) {
	RSVPElement* me = (RSVPElement *) e;
	
	return me->stateTableToString(me->_senders, "Senders");
}

String RSVPElement::reservationsTableHandle(Element *e, void *thunk) {
	RSVPElement* me = (RSVPElement *) e;
	
	return me->stateTableToString(me->_reservations, "Reservations");
}

void RSVPElement::add_handlers() {
	RSVPNode::add_handlers();

	// types of messages
	add_write_handler("path", &pathHandle, (void *) 0);
	add_write_handler("resv", &resvHandle, (void *) 0);
	add_write_handler("patherr", &pathErrHandle, (void *) 0);
	add_write_handler("resverr", &resvErrHandle, (void *) 0);
	add_write_handler("pathtear", &pathTearHandle, (void *) 0);
	add_write_handler("resvtear", &resvTearHandle, (void *) 0);
	add_write_handler("resvconf", &resvConfHandle, (void *) 0);
	add_write_handler("tos", &tosHandle, (void *) 0);
	
	// types of objects
	add_write_handler("session", &sessionHandle, (void *) 0);
	add_write_handler("hop", &hopHandle, (void *)0);
	add_write_handler("errorspec", &errorSpecHandle, (void *) 0);
	add_write_handler("timevalues", &timeValuesHandle, (void *) 0);
	add_write_handler("resvconfobj", &resvConfObjectHandle, (void *) 0);
	add_write_handler("scope", &scopeHandle, (void *) 0);
	add_write_handler("senderdescriptor", &senderDescriptorHandle, (void *) 0);
	add_write_handler("flowdescriptor", &flowDescriptorHandle, (void *) 0);

	// random read handler
	add_read_handler("TTL", &getTTLHandle, (void *) 0);
	
	// other read handlers
	add_read_handler("senderstable", &sendersTableHandle, 0);
	add_read_handler("reservationstable", &reservationsTableHandle, 0);
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

WritablePacket* RSVPElement::createPathMessage(const RSVPSession* p_session,
	const RSVPHop* p_hop,
	const RSVPTimeValues* p_timeValues,
	const RSVPSenderTemplate* p_senderTemplate,
	const RSVPSenderTSpec* p_senderTSpec) const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop) +
		sizeof(RSVPTimeValues) +
		(p_senderTemplate && p_senderTSpec ?
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
	*session = *p_session;
	*hop = *p_hop;
	*timeValues = *p_timeValues;
	if (p_senderTemplate && p_senderTSpec) {
		*senderTemplate = *p_senderTemplate;
		*senderTSpec = *p_senderTSpec;
	}
	
	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

WritablePacket* RSVPElement::createResvMessage() const {
	return createResvMessage(&_session, &_hop, &_timeValues, _resvConf_given ? &_resvConf : NULL,
		_flowDescriptor ? &_flowspec : NULL,
		_flowDescriptor ? &_filterSpec : NULL);
}

WritablePacket* RSVPElement::createResvMessage(const RSVPSession* p_session,
		const RSVPHop* p_hop,
		const RSVPTimeValues* p_timeValues,
		const RSVPResvConf* p_resvConf,
		const RSVPFlowspec* p_flowspec,
		const RSVPFilterSpec* p_filterSpec) const {
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop) +
		sizeof(RSVPTimeValues) +
		(p_resvConf ? sizeof(RSVPResvConf) : 0) +
		sizeofRSVPScopeObject(_scope_src_addresses.size()) +
		sizeof(RSVPStyle) +
		((p_flowspec && p_filterSpec) ? sizeof(RSVPFlowspec) + sizeof(RSVPFilterSpec) : 0);

	WritablePacket* message = createPacket(packetSize);
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPHop* hop                   = (RSVPHop *)          (session      + 1);
	RSVPTimeValues* timeValues     = (RSVPTimeValues *)   (hop          + 1);
	RSVPResvConf* resvConf         = (RSVPResvConf *)     (timeValues   + 1);
	RSVPStyle* style               = (RSVPStyle *)        initRSVPScope((RSVPObjectHeader *) (resvConf + (p_resvConf ? 1 : 0)), _scope_src_addresses);
	RSVPFlowspec* flowspec         = (RSVPFlowspec *)     (style        + 1);
	RSVPFilterSpec* filterSpec     = (RSVPFilterSpec *)   (flowspec     + 1);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_RESV, _TTL, packetSize);
	*session = *p_session;
	*hop = *p_hop;
	*timeValues = *p_timeValues;
	initRSVPStyle(style);
	
	if (p_resvConf) {
		*resvConf = *p_resvConf;
	}
	
	if (p_flowspec && p_filterSpec) {
		*flowspec = *p_flowspec;
		*filterSpec = *p_filterSpec;
	}

	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

WritablePacket* RSVPElement::createPathErrMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPErrorSpec) +
		sizeofRSVPScopeObject(_scope_src_addresses.size()) +
		sizeof(RSVPStyle) +
		(_senderDescriptor ? sizeof(RSVPSenderTemplate) + sizeof(RSVPSenderTSpec) : 0);
	unsigned tailroom = 0;
	
	WritablePacket* message = createPacket(packetSize);
	
	memset(message->data(), 0, message->length());
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPErrorSpec* errorSpec       = (RSVPErrorSpec *)    (session      + 1);
	RSVPSenderTemplate* senderTemplate = (RSVPSenderTemplate *) (errorSpec + 1);
	RSVPSenderTSpec* senderTSpec   = (RSVPSenderTSpec *)  initRSVPScope((RSVPObjectHeader *) (senderTemplate + 1), _scope_src_addresses);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_PATHERR, _TTL, packetSize);
	*session = _session;
	*errorSpec = _errorSpec;
	
	if (_senderDescriptor) {
		*senderTSpec = _senderTSpec;
		*senderTemplate = _senderTemplate;
	}

	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

WritablePacket* RSVPElement::createResvErrMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop) +
		sizeof(RSVPErrorSpec) +
		sizeofRSVPScopeObject(_scope_src_addresses.size()) +
		sizeof(RSVPStyle) +
		(_flowDescriptor ? sizeof(RSVPFilterSpec) + sizeof(RSVPFlowspec) : 0);
	
	WritablePacket* message = createPacket(packetSize);
	
	memset(message->data(), 0, message->length());
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPHop* hop                   = (RSVPHop *)          (session      + 1);
	RSVPErrorSpec* errorSpec       = (RSVPErrorSpec *)    (hop          + 1);
	RSVPStyle* style               = (RSVPStyle *)        initRSVPScope((RSVPObjectHeader *) (errorSpec + 1), _scope_src_addresses);
	RSVPFilterSpec* filterSpec     = (RSVPFilterSpec *)   (style        + 1);
	RSVPFlowspec* flowspec         = (RSVPFlowspec *)     (filterSpec   + 1);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_RESVERR, _TTL, packetSize);
	*session = _session;
	*hop = _hop;
	*errorSpec = _errorSpec;
	initRSVPStyle(style);
	
	if (_flowDescriptor) {
		*filterSpec = _filterSpec;
		*flowspec = _flowspec;
	}

	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

WritablePacket* RSVPElement::createPathTearMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop) +
		(_senderDescriptor ? sizeof(RSVPSenderTemplate) + sizeof(RSVPSenderTSpec) : 0);
	
	WritablePacket* message = createPacket(packetSize);
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPHop* hop                   = (RSVPHop *)          (session      + 1);
	RSVPSenderTemplate* senderTemplate = (RSVPSenderTemplate *) (hop    + 1);
	RSVPSenderTSpec* senderTSpec   = (RSVPSenderTSpec *)  (senderTemplate + 1);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_PATHTEAR, _TTL, packetSize);
	*session = _session;
	initRSVPHop(hop, _myIP, 0);
	
	if (_senderDescriptor) {
		*senderTemplate = _senderTemplate;
		*senderTSpec = _senderTSpec;
	}

	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

WritablePacket* RSVPElement::createResvTearMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPHop) +
		sizeofRSVPScopeObject(_scope_src_addresses.size()) +
		sizeof(RSVPStyle) +
		(_flowDescriptor ? sizeof(RSVPFilterSpec) + sizeof(RSVPFlowspec) : 0);
	
	WritablePacket* message = createPacket(packetSize);
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPHop* hop                   = (RSVPHop *)          (session      + 1);
	RSVPStyle* style               = (RSVPStyle *)        initRSVPScope((RSVPObjectHeader *) (hop + 1), _scope_src_addresses);
	RSVPFilterSpec* filterSpec     = (RSVPFilterSpec *)   (style        + 1);
	RSVPFlowspec* flowspec         = (RSVPFlowspec *)     (filterSpec   + 1);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_RESVTEAR, _TTL, packetSize);
	*session = _session;
	*hop = _hop;
	initRSVPStyle(style);
	
	if (_flowDescriptor) {
		*filterSpec = _filterSpec;
		*flowspec = _flowspec;
	}

	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

WritablePacket* RSVPElement::createResvConfMessage() const
{
	uint16_t packetSize =
		sizeof(RSVPCommonHeader) +
		sizeof(RSVPSession) +
		sizeof(RSVPErrorSpec) +
		sizeof(RSVPResvConf) +
		sizeof(RSVPStyle) +
		(_flowDescriptor ? sizeof(RSVPFilterSpec) + sizeof(RSVPFlowspec) : 0);
	
	WritablePacket* message = createPacket(packetSize);
	
	RSVPCommonHeader* commonHeader = (RSVPCommonHeader *) (message->data());
	RSVPSession* session           = (RSVPSession *)      (commonHeader + 1);
	RSVPErrorSpec* errorSpec       = (RSVPErrorSpec *)    (session      + 1);
	RSVPResvConf* resvConf         = (RSVPResvConf *)     (errorSpec    + 1);
	RSVPStyle* style               = (RSVPStyle *)        (resvConf     + 1);
	RSVPFilterSpec* filterSpec     = (RSVPFilterSpec *)   (style        + 1);
	RSVPFlowspec* flowspec         = (RSVPFlowspec *)     (filterSpec   + 1);
	
	initRSVPCommonHeader(commonHeader, RSVP_MSG_RESVCONF, _TTL, packetSize);
	*session = _session;
	*errorSpec = _errorSpec;
	*resvConf = _resvConf;
	initRSVPStyle(style);
	
	if (_flowDescriptor) {
		*filterSpec = _filterSpec;
		*flowspec = _flowspec;
	}

	commonHeader->RSVP_checksum = click_in_cksum((unsigned char *) commonHeader, packetSize);
	
	return message;
}

void RSVPElement::die() {
	RSVPNode::die();
}

void RSVPElement::clean() {
	_TTL = 250;
	
	memset(&_session, 0, sizeof(RSVPSession));
	memset(&_errorSpec, 0, sizeof(RSVPErrorSpec));
	
	_senderDescriptor = false;
	memset(&_senderTemplate, 0, sizeof(RSVPSenderTemplate));
	memset(&_senderTSpec, 0, sizeof(RSVPSenderTSpec));

	_flowDescriptor = false;
	memset(&_flowspec, 0, sizeof(RSVPFlowspec));
	memset(&_filterSpec, 0, sizeof(RSVPFilterSpec));

	_resvConf_given = false;
	memset(&_resvConf, 0, sizeof(RSVPResvConf));

	_scope_src_addresses.clear();
}	

CLICK_ENDDECLS
EXPORT_ELEMENT(RSVPElement)
