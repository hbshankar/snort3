/*
 * ftpp_si.c
 *
** Copyright (C) 2014 Cisco and/or its affiliates. All rights reserved.
 * Copyright (C) 2004-2013 Sourcefire, Inc.
 * Steven A. Sturges <ssturges@sourcefire.com>
 * Daniel J. Roelker <droelker@sourcefire.com>
 * Marc A. Norton <mnorton@sourcefire.com>
 * Kevin Liu <kliu@sourcefire.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License Version 2 as
 * published by the Free Software Foundation.  You may not use, modify or
 * distribute this program under any other version of the GNU General
 * Public License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Description:
 *
 * This file contains functions to select server configurations
 * and begin the FTPTelnet process.
 *
 * The session Inspection Module interfaces with the Stream Inspection
 * Module and the User Interface Module to select the appropriate
 * FTPTelnet configuration and in the case of stateful inspection the
 * session Inspection Module retrieves the user-data from the Stream
 * Module.  For stateless inspection, the session Inspection Module uses
 * the same structure for use by each packet.
 *
 * The main responsibility of this module is to supply the appropriate
 * data structures and configurations for the rest of the FTPTelnet
 * process.  The module also determines what type of data is being
 * inspected, whether it is client, server, or neither.
 *
 * NOTES:
 * - 20.09.04:  Initial Development.  SAS
 *
 */
#include "ftpp_si.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
# include <ctype.h>

#include "ftpp_return_codes.h"
#include "ftpp_ui_config.h"
#include "snort.h"
#include "stream/stream_api.h"
#include "ft_main.h"

unsigned FtpFlowData::flow_id = 0;
unsigned TelnetFlowData::flow_id = 0;

/*
 * Function: TelnetResetsession(TELNET_SESSION *session)
 *
 * Purpose: This function resets all the variables that need to be
 *          initialized for a new session.  I've tried to keep this to
 *          a minimum, so we don't have to worry about initializing big
 *          structures.
 *
 * Arguments: session         => pointer to the session to reset
 *
 * Returns: int => return code indicating error or success
 *
 */
static inline int TelnetResetsession(TELNET_SESSION *session)
{
    session->ft_ssn.proto = FTPP_SI_PROTO_TELNET;
    session->telnet_conf = NULL;

    session->consec_ayt = 0;
    session->encr_state = NO_STATE;

    return FTPP_SUCCESS;
}

/*
 * Purpose: Initialize the session and server configurations for
 *          this packet/stream.  In this function, we set the session
 *          pointer (which includes the correct server configuration).
 *          The actual processing to find which IP is the server and
 *          which is the client, is done in the InitServerConf() function.
 */
static int TelnetStatefulsessionInspection(Packet *p,
        TELNET_PROTO_CONF* GlobalConf,
        TELNET_SESSION **Telnetsession,
        FTPP_SI_INPUT *SiInput)
{
    if (p->flow)
    {
        TelnetFlowData* fd = new TelnetFlowData;
        TELNET_SESSION* Newsession = &fd->session;

        // FIXIT lots of redundancy; clean up and move to ctor
        TelnetResetsession(Newsession);
        Newsession->ft_ssn.proto = FTPP_SI_PROTO_TELNET;
        Newsession->telnet_conf = GlobalConf;

        SiInput->pproto = FTPP_SI_PROTO_TELNET;

        p->flow->set_application_data(fd);

        *Telnetsession = Newsession;
        return FTPP_SUCCESS;
    }

    return FTPP_NONFATAL_ERR;
}

/*
 * Purpose: The session Inspection module selects the appropriate
 *          configuration for the session, and the type of inspection
 *          to be performed (client or server.)
 *
 *          When the session Inspection module is in stateful mode, it
 *          checks to see if there is a TELNET_SESSION pointer already
 *          associated with the stream.  If there is, then it uses that
 *          session pointer, otherwise it calculates the server configuration
 *          using the FTP_SI_INPUT and returns a TELNET_SESSION pointer.  In
 *          stateful mode, this means that memory is allocated, but in
 *          stateless mode, the same session pointer is used for all packets
 *          to reduce the allocation overhead.
 *
 *          The inspection mode can be either client or server.
 *
 * Arguments: p             => pointer to the packet/stream
 *            GlobalConf    => pointer to the global configuration
 *            session       => double pointer to the session structure
 *            SiInput       => pointer to the session information
 *            piInspectMode => pointer for setting inspection mode
 *
 * Returns: int => return code indicating error or success
 *
 */
int TelnetsessionInspection(Packet *p, TELNET_PROTO_CONF* GlobalConf,
        TELNET_SESSION **Telnetsession, FTPP_SI_INPUT *SiInput, int *piInspectMode)
{
    int iRet;

    if (SiInput->pdir == FTPP_SI_CLIENT_MODE ||
        SiInput->pdir == FTPP_SI_SERVER_MODE)
    {
        *piInspectMode = (int)SiInput->pdir;
    }

    /*
     * We get the server configuration and the session structure differently
     * depending on what type of inspection we are doing.  In the case of
     * stateful processing, we may get the session structure from the Stream
     * Reassembly module (which includes the server configuration) or the
     * structure will be allocated and added to the stream pointer for the
     * rest of the session.
     */
    iRet = TelnetStatefulsessionInspection(p, GlobalConf, Telnetsession, SiInput);
    if (iRet)
        return iRet;

    return FTPP_SUCCESS;
}

/*
 * Function: FTPGetPacketDir(Packet *p)
 *
 * Purpose: Attempts to determine the direction of an FTP packet by
 *          examining the first 3 bytes.  If all three are numeric,
 *          the packet is a server response packet.
 *
 * Arguments: p             => pointer to the Packet
 *
 * Returns: int => return code indicating the mode
 *
 */
int FTPGetPacketDir(Packet *p)
{
    if (p->dsize >= 3)
    {
        if (isdigit(p->data[0]) &&
            isdigit(p->data[1]) &&
            isdigit(p->data[2]) )
        {
            return FTPP_SI_SERVER_MODE;
        }
        else
        {
            return FTPP_SI_CLIENT_MODE;
        }
    }
    return FTPP_SI_NO_MODE;
}

/*
 * // FIXIT this goes away when bindings are implemented
 * Purpose: When a session is initialized, we must select the appropriate
 *          server configuration and select the type of inspection based
 *          on the source and destination ports.
 *
 * IMPORTANT NOTE:
 *   We should check to make sure that there are some unique configurations,
 *   otherwise we can just default to the global default and work some magic
 *   that way.
 */
static int FTPInitConf(
    Packet *p, 
    FTP_CLIENT_PROTO_CONF **ClientConf,
    FTP_SERVER_PROTO_CONF **ServerConf,
    FTPP_SI_INPUT *SiInput, int *piInspectMode)
{
    // FIXIT BINDING ftp client and server must set by external bindings
    // at that point these get deleted
    FTP_CLIENT_PROTO_CONF *ClientConfSip = get_default_ftp_client();
    FTP_CLIENT_PROTO_CONF *ClientConfDip = get_default_ftp_client();

    FTP_SERVER_PROTO_CONF *ServerConfSip = get_default_ftp_server();
    FTP_SERVER_PROTO_CONF *ServerConfDip = get_default_ftp_server();

    int iServerSip;
    int iServerDip;
    int iRet = FTPP_SUCCESS;

    /*
     * We check the IP and the port to see if the FTP client is talking in
     * the session.  This should tell us whether it is client communication
     * or server configuration.  If both IPs and ports are servers, then there
     * is a sort of problem.  We don't know which side is the client and which
     * side is the server so we have to assume one.
     *
     * In stateful processing, we only do this stage on the startup of a
     * session, so we can still assume that the initial packet is the client
     * talking.
     */
    iServerDip = (p->packet_flags & PKT_FROM_CLIENT);
    iServerSip = (p->packet_flags & PKT_FROM_SERVER);

    /*
     * We default to the no FTP traffic case
     */
    *piInspectMode = FTPP_SI_NO_MODE;
    *ClientConf = NULL;
    *ServerConf = NULL;

    /*
     * Depending on the type of packet direction we get from the
     * state machine, we evaluate client/server differently.
     */
    switch(SiInput->pdir)
    {
        case FTPP_SI_NO_MODE:
        {
            /*
             * We check for the case where both SIP and DIP
             * appear to be servers.  In this case, we assume server
             * and process that way.
             */
            if(iServerSip && iServerDip)
            {
                /*
                 * We check for the case where both SIP and DIP
                 * appear to be servers.  In this case, we look at
                 * the first few bytes of the packet to try to
                 * determine direction -- 3 digits indicate server
                 * response.
                 */

                /* look at the first few bytes of the packet.  We might
                 * be wrong if this is a reassembled packet and we catch
                 * a server response mid-stream.
                 */
                *piInspectMode = FTPGetPacketDir(p);
                if (*piInspectMode == FTPP_SI_SERVER_MODE)
                {
                    /* Packet is from server --> src is Server */
                    *ClientConf = ClientConfDip;
                    *ServerConf = ServerConfSip;
                }
                else /* Assume client */
                {
                    /* Packet is from client --> dest is Server */
                    *piInspectMode = FTPP_SI_CLIENT_MODE;
                    *ClientConf = ClientConfSip;
                    *ServerConf = ServerConfDip;
                }
                SiInput->pproto = FTPP_SI_PROTO_FTP;
            }
            else if(iServerDip)
            {
                /* Packet is from client --> dest is Server */
                *piInspectMode = FTPP_SI_CLIENT_MODE;
                *ClientConf = ClientConfSip;
                *ServerConf = ServerConfDip;
                SiInput->pproto = FTPP_SI_PROTO_FTP;
            }
            else if(iServerSip)
            {
                /* Packet is from server --> src is Server */
                *piInspectMode = FTPP_SI_SERVER_MODE;
                *ClientConf = ClientConfDip;
                *ServerConf = ServerConfSip;
                SiInput->pproto = FTPP_SI_PROTO_FTP;
            }
            break;

        }

        case FTPP_SI_CLIENT_MODE:
            /* Packet is from client --> dest is Server */
            *piInspectMode = FTPP_SI_CLIENT_MODE;
            *ClientConf = ClientConfSip;
            *ServerConf = ServerConfDip;
            SiInput->pproto = FTPP_SI_PROTO_FTP;
            break;

        case FTPP_SI_SERVER_MODE:
            /* Packet is from server --> src is Server */
            *piInspectMode = FTPP_SI_SERVER_MODE;
            *ClientConf = ClientConfDip;
            *ServerConf = ServerConfSip;
            SiInput->pproto = FTPP_SI_PROTO_FTP;
            break;

        default:
            *piInspectMode = FTPP_SI_NO_MODE;
            *ClientConf = NULL;
            *ServerConf = NULL;
            break;
    }

    return iRet;
}

/*
 * Function: FTPFreesession(void *preproc_session)
 *
 * Purpose: This function frees the data that is associated with a session.
 *
 * Arguments: preproc_session   => pointer to the session to free
 *
 * Returns: None
 */
void FTPFreesession(FTP_SESSION* ssn)
{
    if (ssn->filename)
        free(ssn->filename);
}

unsigned FtpDataFlowData::flow_id = 0;

FtpDataFlowData::FtpDataFlowData(Packet* p) : FlowData(flow_id)
{
    memset(&session, 0, sizeof(session));

    session.ft_ssn.proto = FTPP_SI_PROTO_FTP_DATA;
    stream.populate_session_key(p, &session.ftp_key);
}

FtpDataFlowData::~FtpDataFlowData()
{
    if (session.filename)
        free(session.filename);
}

/* Function: FTPDataDirection
 *
 * Return true if packet is from the "sending" host
 * Return false if packet is from the "receiving" host
 */
bool FTPDataDirection(Packet *p, FTP_DATA_SESSION *ftpdata)
{
    uint32_t direction;
    uint32_t pktdir = stream.get_packet_direction(p); 

    if (ftpdata->mode == FTPP_XFER_ACTIVE)
        direction = ftpdata->direction ?  PKT_FROM_SERVER : PKT_FROM_CLIENT;
    else
        direction = ftpdata->direction ?  PKT_FROM_CLIENT : PKT_FROM_SERVER;

    return (pktdir == direction);
}

/* Function: SetFTPDataEOFDirection
 *
 * Set EOF in the direction of Packet "p".
 */
void SetFTPDataEOFDirection(Packet *p, FTP_DATA_SESSION *ftpdata)
{
    uint32_t direction = stream.get_packet_direction(p);

    /* Active transfers are backwards */
    if (ftpdata->mode == FTPP_XFER_ACTIVE)
    {
        if (direction == PKT_FROM_SERVER)
        {
            ftpdata->packet_flags |= FTPDATA_FLG_CLIENT_EOF;
        }
        else
        {
            ftpdata->packet_flags |= FTPDATA_FLG_SERVER_EOF;
        }
    }
    else
    {
        if (direction == PKT_FROM_SERVER)
        {
            ftpdata->packet_flags |= FTPDATA_FLG_SERVER_EOF;
        }
        else
        {
            ftpdata->packet_flags |= FTPDATA_FLG_CLIENT_EOF;
        }
    }
}

/* Function: FTPDataEOFDirection
 *
 * Return true if we've seen EOF in the direction of Packet "p".
 * Return false otherwise
 */
bool FTPDataEOFDirection(Packet *p, FTP_DATA_SESSION *ftpdata)
{
    uint32_t direction = stream.get_packet_direction(p);
    uint32_t eof = 0;

    /* Then return which directions are set */
    if (ftpdata->mode == FTPP_XFER_ACTIVE)
    {
        if (ftpdata->packet_flags & FTPDATA_FLG_CLIENT_EOF)
            eof |= PKT_FROM_SERVER;

        if (ftpdata->packet_flags & FTPDATA_FLG_SERVER_EOF)
            eof |= PKT_FROM_CLIENT;
    }
    else
    {
        if (ftpdata->packet_flags & FTPDATA_FLG_CLIENT_EOF)
            eof |= PKT_FROM_CLIENT;

        if (ftpdata->packet_flags & FTPDATA_FLG_SERVER_EOF)
            eof |= PKT_FROM_SERVER;
    }

    return ( (direction & eof) != 0 );
}

/* Function: FTPDataEOF
 *
 * Return true if we've seen EOF from both sides of the connection.
 * Return false otherwise.
 */
bool FTPDataEOF(FTP_DATA_SESSION *ftpdata)
{
    unsigned char mask = FTPDATA_FLG_SERVER_EOF|FTPDATA_FLG_CLIENT_EOF;
    return ((ftpdata->packet_flags & mask) == mask);
}

/*
 * Function: FTPResetsession(FTP_SESSION *Ftpsession, int first)
 *
 * Purpose: This function resets all the variables that need to be
 *          initialized for a new session.  I've tried to keep this to
 *          a minimum, so we don't have to worry about initializing big
 *          structures.
 *
 * Arguments: Ftpsession    => pointer to the session to reset
 *            first         => indicator whether this is a new conf
 *
 * Returns: int => return code indicating error or success
 *
 */
static inline int FTPResetsession(FTP_SESSION *Ftpsession)
{
    Ftpsession->ft_ssn.proto = FTPP_SI_PROTO_FTP;

    Ftpsession->server.response.pipeline_req = 0;
    Ftpsession->server.response.state = 0;
    Ftpsession->client.request.pipeline_req = 0;
    Ftpsession->client.state = 0;

    Ftpsession->client_conf = NULL;
    Ftpsession->server_conf = NULL;

    Ftpsession->encr_state = NO_STATE;
    IP_CLEAR(Ftpsession->clientIP);
    Ftpsession->clientPort = 0;
    IP_CLEAR(Ftpsession->serverIP);
    Ftpsession->serverPort = 0;
    Ftpsession->data_chan_state = NO_STATE;
    Ftpsession->data_chan_index = -1;
    Ftpsession->data_xfer_index = -1;

    return FTPP_SUCCESS;
}

/*
 * Purpose: Initialize the session and server configurations for this
 *          packet/stream.  In this function, we set the session pointer
 *          (which includes the correct server configuration).  The actual
 *          processing to find which IP is the server and which is the
 *          client, is done in the InitServerConf() function.
 */
static int FTPStatefulsessionInspection(
    Packet *p,
    FTP_SESSION **Ftpsession,
    FTPP_SI_INPUT *SiInput, int *piInspectMode)
{
    if (p->flow)
    {
        FTP_CLIENT_PROTO_CONF *ClientConf;
        FTP_SERVER_PROTO_CONF *ServerConf;
        int iRet;

        iRet = FTPInitConf(p, &ClientConf, &ServerConf, SiInput, piInspectMode);
        if (iRet)
            return iRet;

        if (*piInspectMode)
        {
            FtpFlowData* fd = new FtpFlowData;
            FTP_SESSION* Newsession = &fd->session;

            // FIXIT lots of redundancy; clean up and move to ctor
            FTPResetsession(Newsession);
            Newsession->ft_ssn.proto = FTPP_SI_PROTO_FTP;
            Newsession->client_conf = ClientConf;
            Newsession->server_conf = ServerConf;

            p->flow->set_application_data(fd);

            *Ftpsession = Newsession;
            SiInput->pproto = FTPP_SI_PROTO_FTP;
            return FTPP_SUCCESS;
        }
    }

    return FTPP_INVALID_PROTO;
}

/*
 * Purpose: The session Inspection module selects the appropriate client
 *          configuration for the session, and the type of inspection to
 *          be performed (client or server.)
 *
 *          When the session Inspection module is in stateful mode, it
 *          checks to see if there is a FTP_SESSION pointer already
 *          associated with the stream.  If there is, then it uses that
 *          session pointer, otherwise it calculates the server
 *          configuration using the FTP_SI_INPUT and returns a FTP_SESSION
 *          pointer.  In stateful mode, this means that memory is allocated,
 *          but in stateless mode, the same session pointer is used for all
 *          packets to reduce the allocation overhead.
 *
 *          The inspection mode can be either client or server.
 */
int FTPsessionInspection(
    Packet *p, FTP_SESSION **Ftpsession,
    FTPP_SI_INPUT *SiInput, int *piInspectMode)
{
    int iRet;

    /*
     * We get the server configuration and the session structure differently
     * depending on what type of inspection we are doing.  In the case of
     * stateful processing, we may get the session structure from the Stream
     * Reassembly module (which includes the server configuration) or the
     * structure will be allocated and added to the stream pointer for the
     * rest of the session.
     */
    iRet = FTPStatefulsessionInspection(p, Ftpsession, SiInput, piInspectMode);
    if (iRet)
        return iRet;

    return FTPP_SUCCESS;
}

/*
 * Function: SetSiInput(FTPP_SI_INPUT *SiInput, Packet *p)
 *      
 * Purpose: This is the routine sets the source and destination IP
 *          address and port pairs so as to determine the direction
 *          of the FTP or telnet connection.
 *
 * Arguments: SiInput       => pointer the session input structure
 *            p             => pointer to the packet structure
 *      
 * Returns: int     => an error code integer (0 = success,
 *                     >0 = non-fatal error, <0 = fatal error)
 *  
 */
int SetSiInput(FTPP_SI_INPUT *SiInput, Packet *p)
{   
    IP_COPY_VALUE(SiInput->sip, GET_SRC_IP(p));
    IP_COPY_VALUE(SiInput->dip, GET_DST_IP(p));
    SiInput->sport = p->sp;
    SiInput->dport = p->dp;
        
    /* 
     * We now set the packet direction
     */
    if(p->flow && stream.is_midstream(p->flow))
    {
        SiInput->pdir = FTPP_SI_NO_MODE;
    }
    else if(p->packet_flags & PKT_FROM_SERVER)
    {
        SiInput->pdir = FTPP_SI_SERVER_MODE;
    }
    else if(p->packet_flags & PKT_FROM_CLIENT)
    {
        SiInput->pdir = FTPP_SI_CLIENT_MODE;
    }
    else
    {       
        SiInput->pdir = FTPP_SI_NO_MODE;
    }

    return FTPP_SUCCESS;

}

