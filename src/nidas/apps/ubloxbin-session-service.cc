// -*- mode: C++; indent-tabs-mode: nil; c-basic-offset: 4; tab-width: 4; -*-
// vim: set shiftwidth=4 softtabstop=4 expandtab:
/*
 ********************************************************************
 ** NIDAS: NCAR In-situ Data Acquistion Software
 **
 ** 2006, Copyright University Corporation for Atmospheric Research
 **
 ** This program is free software; you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation; either version 2 of the License, or
 ** (at your option) any later version.
 **
 ** This program is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** The LICENSE.txt file accompanying this software contains
 ** a copy of the GNU General Public License. If it is not found,
 ** write to the Free Software Foundation, Inc.,
 ** 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 **
 ********************************************************************
*/

/*
 * Program to set options on a u-blox GPS.
 * Written using docs for a u-blox model M8Q, 
 * but likely will work for most.
 * 
 * NOTE: This particular vesion of ubloxbin is an experiment to figure 
 *       out how to reset the asio serial port after a baud rate change.
 */

#include <nidas/util/time_constants.h>
#include <nidas/util/Logger.h>
#include <nidas/core/NidasApp.h>

using namespace nidas::core;
using namespace nidas::util;

#include <iostream>
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/array.hpp>
#include <boost/thread.hpp>

#include "ublox/Message.h"
#include "ublox/frame/UbloxFrame.h"
#include "comms/units.h"
#include "comms/process.h"

using namespace boost::asio;

enum NMEA_MsgId : std::uint16_t
{
    MsgId_GGA = 0xF000,        // GPS fix data
    MsgId_GLL = 0xF001,        // Lat/long w/time of fix and status
    MsgId_GSA = 0xF002,        // GNSS DOP and active sats
    MsgId_GSV = 0xF003,        // GNSS sats in view
    MsgId_RMC = 0xF004,        // Recommended minimum data
    MsgId_VTG = 0xF005,        // Course over ground and ground speed
    MsgId_GRS = 0xF006,        // GNSS range residuals
    MsgId_GST = 0xF007,        // GNSS pseudo range error stats
    MsgId_ZDA = 0xF008,        // Time and date
    MsgId_GBS = 0xF009,        // GNSS satellite fault detection
    MsgId_DTM = 0xF00A,        // Datum message
    MsgId_GNS = 0xF00D,        // GNSS fix data
    MsgId_VLW = 0xF00F,        // Dual ground/water distance
    // MsgId_GPQ = 0xF040,        // Poll a standard message if talker ID is GP
    // MsgId_TXT = 0xF041,        // Text xmission
    // MsgId_GNQ = 0xF042,        // Poll a standard message if talker ID is GN
    // MsgId_GLQ = 0xF043,        // Poll a standard message if talker ID is GL
    // MsgId_GBQ = 0xF044,        // poll a standard message if talker ID is GB
    MsgId_PBX_POS = 0xF100,    // Lat/long position
    MsgId_PBX_SVSTAT = 0xF103, // Satellite status
    MsgId_PBX_TIME = 0xF104,   // Time of day and clock data
    MsgId_PBX_RATE = 0xF140,   // NMEA output rate
    MsgId_PBX_CFG = 0xF141,    // Set protocols and baudrate
    MsgId_Min = MsgId_GGA,
    MsgId_Max = MsgId_PBX_CFG,
};

NMEA_MsgId All_NMEA_IDs[] = 
{
    MsgId_GBS,
    MsgId_GGA,
    MsgId_GLL,
    // MsgId_GNQ,
    MsgId_GNS,
    MsgId_GRS,
    MsgId_GSA,
    MsgId_GST,
    MsgId_GSV,
    MsgId_RMC,
    // MsgId_TXT,
    MsgId_VLW,
    MsgId_VTG,
    MsgId_ZDA,
    MsgId_DTM,
    // MsgId_GPQ,
    // MsgId_GLQ,
    // MsgId_GBQ,
    MsgId_PBX_POS,
    MsgId_PBX_SVSTAT,
    MsgId_PBX_TIME,
};

ublox::MsgId All_UBX_IDs[] = 
{
    ublox::MsgId_NavPosecef,
    ublox::MsgId_NavPosllh,
    ublox::MsgId_NavStatus,
    ublox::MsgId_NavDop,
    // ublox::MsgId_NavAtt,
    ublox::MsgId_NavSol,
    ublox::MsgId_NavPvt,
    ublox::MsgId_NavOdo,
    // ublox::MsgId_NavResetodo,
    ublox::MsgId_NavVelecef,
    ublox::MsgId_NavVelned,
    // ublox::MsgId_NavHpposecef,
    // ublox::MsgId_NavHpposllh,
    ublox::MsgId_NavTimegps,
    ublox::MsgId_NavTimeutc,
    ublox::MsgId_NavClock,
    ublox::MsgId_NavTimeglo,
    ublox::MsgId_NavTimebds,
    ublox::MsgId_NavTimegal,
    ublox::MsgId_NavTimels,
    ublox::MsgId_NavSvinfo,
    ublox::MsgId_NavDgps,
    ublox::MsgId_NavSbas,
    ublox::MsgId_NavOrb,
    ublox::MsgId_NavSat,
    ublox::MsgId_NavGeofence,
    // ublox::MsgId_NavSvin,
    // ublox::MsgId_NavRelposned,
    // ublox::MsgId_NavEkfstatus,
    ublox::MsgId_NavAopstatus,
    ublox::MsgId_NavEoe,
    // ublox::MsgId_RxmRaw,
    // ublox::MsgId_RxmSfrb,
    ublox::MsgId_RxmSfrbx,
    ublox::MsgId_RxmMeasx,
    ublox::MsgId_RxmRawx,
    ublox::MsgId_RxmSvsi,
    // ublox::MsgId_RxmAlm,
    // ublox::MsgId_RxmEph,
    // ublox::MsgId_RxmRtcm,
    // ublox::MsgId_RxmPmreq,
    ublox::MsgId_RxmRlm,
    ublox::MsgId_RxmImes,
    // ublox::MsgId_InfError,
    // ublox::MsgId_InfWarning,
    // ublox::MsgId_InfNotice,
    // ublox::MsgId_InfTest,
    // ublox::MsgId_InfDebug,
    // ublox::MsgId_AckNak,
    // ublox::MsgId_AckAck,
    // ublox::MsgId_CfgPrt,
    // ublox::MsgId_CfgMsg,
    // ublox::MsgId_CfgInf,
    // ublox::MsgId_CfgRst,
    // ublox::MsgId_CfgDat,
    // ublox::MsgId_CfgTp,
    // ublox::MsgId_CfgRate,
    // ublox::MsgId_CfgCfg,
    // ublox::MsgId_CfgFxn,
    // ublox::MsgId_CfgRxm,
    // ublox::MsgId_CfgEkf,
    // ublox::MsgId_CfgAnt,
    // ublox::MsgId_CfgSbas,
    // ublox::MsgId_CfgNmea,
    // ublox::MsgId_CfgUsb,
    // ublox::MsgId_CfgTmode,
    // ublox::MsgId_CfgOdo,
    // ublox::MsgId_CfgNvs,
    // ublox::MsgId_CfgNavx5,
    // ublox::MsgId_CfgNav5,
    // ublox::MsgId_CfgEsfgwt,
    // ublox::MsgId_CfgTp5,
    // ublox::MsgId_CfgPm,
    // ublox::MsgId_CfgRinv,
    // ublox::MsgId_CfgItfm,
    // ublox::MsgId_CfgPm2,
    // ublox::MsgId_CfgTmode2,
    // ublox::MsgId_CfgGnss,
    // ublox::MsgId_CfgLogfilter,
    // ublox::MsgId_CfgTxslot,
    // ublox::MsgId_CfgPwr,
    // ublox::MsgId_CfgHnr,
    // ublox::MsgId_CfgEsrc,
    // ublox::MsgId_CfgDosc,
    // ublox::MsgId_CfgSmgr,
    // ublox::MsgId_CfgGeofence,
    // ublox::MsgId_CfgDgnss,
    // ublox::MsgId_CfgTmode3,
    // ublox::MsgId_CfgFixseed,
    // ublox::MsgId_CfgDynseed,
    // ublox::MsgId_CfgPms,
    // ublox::MsgId_UpdSos,
    // ublox::MsgId_MonGnss,
    ublox::MsgId_MonIo,
    // ublox::MsgId_MonVer,
    ublox::MsgId_MonHw,
    ublox::MsgId_MonHw2,
    ublox::MsgId_MonIo,
    ublox::MsgId_MonMsgpp,
    // ublox::MsgId_MonPatch,
    ublox::MsgId_MonRxbuf,
    // ublox::MsgId_MonSmgr,
    ublox::MsgId_MonTxbuf,
    ublox::MsgId_MonRxr,
    ublox::MsgId_AidAlm,
    // ublox::MsgId_AidAlp,
    // ublox::MsgId_AidAlpsrv,
    ublox::MsgId_AidAop,
    // ublox::MsgId_AidData,
    // ublox::MsgId_AidEph,
    // ublox::MsgId_AidHui,
    // ublox::MsgId_AidIni,
    // ublox::MsgId_AidReq,
    // ublox::MsgId_TimDosc,
    // ublox::MsgId_TimFchg,
    // ublox::MsgId_TimHoc,
    // ublox::MsgId_TimSmeas,
    ublox::MsgId_TimSvin,
    ublox::MsgId_TimTm2,
    // ublox::MsgId_TimTos,
    ublox::MsgId_TimTp,
    // ublox::MsgId_TimVcocal,
    // ublox::MsgId_TimVrfy,
    // ublox::MsgId_EsfIns,
    // ublox::MsgId_EsfMeas,
    // ublox::MsgId_EsfRaw,
    // ublox::MsgId_EsfStatus,
    // ublox::MsgId_MgaGps,
    // ublox::MsgId_MgaGal,
    // ublox::MsgId_MgaBds,
    // ublox::MsgId_MgaQzss,
    // ublox::MsgId_MgaGlo,
    // ublox::MsgId_MgaAno,
    // ublox::MsgId_MgaFlash,
    // ublox::MsgId_MgaIni,
    // ublox::MsgId_MgaAck,
    // ublox::MsgId_MgaDbd,
    // ublox::MsgId_LogErase,
    // ublox::MsgId_LogString,
    // ublox::MsgId_LogCreate,
    // ublox::MsgId_LogInfo,
    // ublox::MsgId_LogRetrieve,
    // ublox::MsgId_LogRetrievepos,
    // ublox::MsgId_LogRetrievestring,
    // ublox::MsgId_LogFindtime,
    // ublox::MsgId_LogRetrieveposextra,
    // ublox::MsgId_SecSign,
    // ublox::MsgId_SecUniqid,
    // ublox::MsgId_HnrPvt,
};

static bool spyOnCapturedInput = false;

class Session 
{
    using InMessage =
        ublox::Message<
            comms::option::ReadIterator<const std::uint8_t*> //,
            //comms::option::Handler<Session> // Dispatch to this object
        >;

    using OutBuffer = std::vector<std::uint8_t>;
    using OutMessage =
        ublox::Message<
            comms::option::IdInfoInterface,
            comms::option::WriteIterator<std::back_insert_iterator<OutBuffer> >,
            comms::option::LengthInfoInterface
        >;

    using InNavPvt = ublox::message::NavPvt<InMessage>;
    using InTimTp = ublox::message::TimTp<InMessage>;
    using InAckAck = ublox::message::AckAck<InMessage>;
    using InAckNak = ublox::message::AckNak<InMessage>;
    using MsgId = ublox::MsgId;

    // MsgId a = MsgId::MsgId_TimTp;

    using AllHandledInMessages =
        std::tuple<
            InNavPvt,
            InAckNak,
            InAckAck,
            InTimTp
        >;

    using Frame = ublox::frame::UbloxFrame<InMessage, AllHandledInMessages>;

    using SerialPort = boost::asio::serial_port;
    using IoService = boost::asio::io_service;

public:
    Session(const std::string& dev)
      : m_pSerial(), m_pIoService(), m_device(dev), m_inputBuf(), m_inData(), 
        m_frame(), m_waitingAckMsg(false)m_sentMsgIdId((MsgId)0), msgAcked(false),
        enabledMsgs(), checkEnabledMsgs(false), ackCheck(false), m_detectedBaudRate(0)
    {}

    ~Session() = default;

    bool getCheckEnabledMsgs() {return checkEnabledMsgs;}
    void setCheckEnabledMsgs(bool check = true) {checkEnabledMsgs = check;}
    bool getAckCheck() {return ackCheck;}
    void setAckCheck(bool check = true) {ackCheck = check;}

    bool start()
    {
        bool retval = initIoService();
        retval &= initSerial(9600);
        return retval;
    }

    bool initSerial(int baud)
    {
        if (m_pIoService.get()) {
            m_pSerial.reset(new SerialPort(*m_pIoService));
        }
        else 
        {
            return false;
        }

        if (!m_pSerial.get()) {
            return false;
        }
        
        boost::system::error_code ec;
        m_pSerial->open(m_device, ec);
        if (ec) {
            std::cerr << "ERROR: Failed to open " << m_device << std::endl;
            return false;
        }

        m_pSerial->set_option(SerialPort::baud_rate(baud));
        m_pSerial->set_option(SerialPort::character_size(8));
        m_pSerial->set_option(SerialPort::parity(SerialPort::parity::none));
        m_pSerial->set_option(SerialPort::stop_bits(SerialPort::stop_bits::one));
        m_pSerial->set_option(SerialPort::flow_control(SerialPort::flow_control::none));

 #if BOOST_ASIO_VERSION < 101200 && defined(__linux__)
        // Workaround to set some options for the port manually. This is done in
        // Boost.ASIO, but until v1.12.0 (Boost 1.66) there was a bug which doesn't enable relevant
        // code. Fixed by commit: https://github.com/boostorg/asio/commit/619cea4356
        {
            int fd = m_pSerial->native_handle();

            termios tio;
            tcgetattr(fd, &tio);

            // Set serial port to "raw" mode to prevent EOF exit.
            cfmakeraw(&tio);

            // Commit settings
            tcsetattr(fd, TCSANOW, &tio);
        }
 #endif

        return true;
    }

    bool initIoService()
    {
        m_pIoService.reset(new IoService);
        if (!m_pIoService.get()) {
            return false;
        }

        return true;
    }

    IoService& getIoService()
    {
        assert(m_pIoService.get());
        return *m_pIoService;
    }

    void handle(InNavPvt& msg)
    {
        int year = msg.field_year().value();
        int month = msg.field_month().value();
        int day = msg.field_day().value();
        int hours = msg.field_hour().value();
        int minutes = msg.field_min().value();
        int seconds = msg.field_sec().value();
        char dateBuf[50];
        memset(dateBuf, 0, 50);
        sprintf(dateBuf, "%d%02d%02d - %d:%02d:%02d", year, month, day, hours, minutes, seconds);
        std::string date(dateBuf, strlen(dateBuf));

        std::string fix(ublox::field::GpsFixCommon::valueName(msg.field_fixType().value()));
        auto& validField = msg.field_valid(); //asBitmask();
        using OutValidFieldMask = typename std::decay<decltype(validField)>::type;
        bool validDate = validField.getBitValue(OutValidFieldMask::BitIdx_validDate);
        bool validTime = validField.getBitValue(OutValidFieldMask::BitIdx_validTime);
        bool resolvedTime = validField.getBitValue(OutValidFieldMask::BitIdx_fullyResolved);

        DLOG(("NAV-PVT: fix=") << fix 
            << "; lat=" << comms::units::getDegrees<double>(msg.field_lat()) 
            << "; lon=" << comms::units::getDegrees<double>(msg.field_lon()) 
            << "; alt=" << comms::units::getMeters<double>(msg.field_height()) 
            << "; UTC date: " << date 
            << (validDate ? " valid" : " invalid") << " date" 
            << (validTime ? " valid" : " invalid") << " time" 
            << (resolvedTime ? " fully resolved" : " not fully resolved") << " time");

        std::string msgName(msg.doName());
        if (getCheckEnabledMsgs() && msgEnabled(msgName)) {
            enabledMsgs[msgName]++;
        }
    }

    void handle(InTimTp& msg)
    {
        DLOG(("Session::handle(InTimTp&): Caught TimTp message..."));

        // auto& refInfo = msg.field_refInfo().value();


        std::string msgName(msg.doName());
        if (getCheckEnabledMsgs() && msgEnabled(msgName)) {
            enabledMsgs[msgName]++;
        }
    }

    void handle(InAckAck& msg)
    {
        int ackMsgId = msg.field_msgId().value();
        char buf[32];
        memset(buf, 0, 32);
        sprintf(buf, "0x%04X", ackMsgId);
        std::string ackMsgIdStr(buf, strlen(buf));
        if (getAckCheck()) {
            if (ackMsgId == m_sentMsgId) {
                m_waitingAckMsg = false;
                msgAcked = true;
                m_sentMsgId = ublox::MsgId_AckAck;
            }
        }
        VLOG(("AckAck caught for ") << ackMsgIdStr);
    }

    void handle(InAckNak& msg)
    {
        int nakMsgId = msg.field_msgId().value();
        char buf[32];
        memset(buf, 0, 32);
        sprintf(buf, "0x%04X", nakMsgId);
        std::string nakMsgIdStr(buf, strlen(buf));
        VLOG(("AckNak caught for ") << nakMsgIdStr);
        if (getAckCheck()) {
            if (nakMsgId == m_sentMsgId) {
                m_waitingAckMsg = false;
                m_sentMsgId = ublox::MsgId_AckNak;
            }
        }
    }

    void handle(InMessage& /*msg*/)
    {
        // ignore all other incoming
        DLOG(("Session::handle(\"InMessage&\"): caught unhandled message"));
    }

    void performRead()
    {
        m_pSerial->async_read_some(
            boost::asio::buffer(m_inputBuf),
            [this](const boost::system::error_code& ec, std::size_t bytesCount)
            {
                switch (ec.value()) {
                    case boost::asio::error::operation_aborted:
                        VLOG(("Session::performRead(): aborted"));
                        return;
                        break;
                    case boost::asio::error::eof:
                        VLOG(("Session::performRead(): eof - should never happen..."));
                        return;
                        break;
                }

                if (bytesCount) {
                    VLOG(("Session::performRead(): caught ") << bytesCount << " bytes...");

                    if (spyOnCapturedInput) {
                        VLOG(("Session::performRead(): spying on captured data..."));
                        std::string hexStr;
                        auto inputIter = m_inputBuf.begin();
                        auto inputEndIter = inputIter + bytesCount;
                        while (inputIter++ != inputEndIter) {
                            char hexchar[10] = {0,0,0,0,0,0,0,0,0,0};
                            snprintf(hexchar, 9, "0x%02X ", *inputIter);
                            hexStr.append(hexchar, 5);
                        }
                        VLOG(("Session::performRead(): input buf: ") << hexStr);
                    }

                    auto dataBegIter = m_inputBuf.begin();
                    auto dataEndIter = dataBegIter + bytesCount;
                    m_inData.insert(m_inData.end(), dataBegIter, dataEndIter);

                    processInputData();
                    performRead();
                }
                else {
                    VLOG(("Session::performRead(): No data found, getting out..."));
                }
            });
    }   

    bool findBaudRate()
    {
        DLOG(("Session::findBaudRate(): find baud rate by trying to disable a message, and check for ACK"));
        using OutCfgMsgCurrent = ublox::message::CfgMsgCurrent<OutMessage>;
        DLOG(("Session::findBaudRate(): checking for response @ 9600 baud"));
        OutCfgMsgCurrent msg;
        msg.field_msgId().value() = ublox::MsgId_NavPvt;
        msg.field_rate().value() = 0;
        sendMessage(msg);
        if (m_waitingAckMsg || !msgAcked) {
            DLOG(("Session::findBaudRate(): No response @ 9600 baud, checking for response @ 115200 baud"));
            m_pSerial->set_option(SerialPort::baud_rate(115200));
            sendMessage(msg);
            if (m_waitingAckMsg || !msgAcked) {
                DLOG(("Session::findBaudRate(): No response @ 115200 baud - failed"));
                return false;
            }
            else {
                m_detectedBaudRate = 115200;
            }
        }
        else {
            m_detectedBaudRate = 9600;
        }
        return true;
    }

    void configureUbx()
    {
        configureUbxProtocol(true); // use the detected baud rate
        disableAllMessages();  // seems all UBX messages are disabled by default, too.
        // spyOnCapturedInput = true;
        configGnss();
        configureUbxPowerMode();
        configureUbxRTCUpdate();
        // spyOnCapturedInput = false;
        configureUbxNavMode();
        configurePPS();
        enableDefaultMessages();
        configureUbxProtocol(false); // force switch to 115200 baud
    }

    bool msgEnabled(const std::string& name) const
    {
        return (enabledMsgs.find(name) != enabledMsgs.end());
    }

    bool testEnabledMsgs()
    {
        if (getCheckEnabledMsgs()) {
            DLOG(("Session::testEnabledMsgs(): enabled"));
            bool done = true;
            using MsgItem = std::pair<std::string, int>;
            for (MsgItem msg : enabledMsgs) {
                ILOG(("Session::testEnabledMsgs(): msg: ") << msg.first << " - received: " << msg.second);
                done &= (msg.second > 1);
            }

            ILOG(("Session::testEnabledMsgs(): ") << (done ? "done" : "not done." ));
            return done;
        }
        else {
            DLOG(("Session::testEnabledMsgs(): disabled"));
            return false;
        }
    }

    void printEnabledMsgs()
    {
        std::cout << std::endl 
                  << "Enabled Messages" << std::endl 
                  << "================" << std::endl;
        using MsgItem = std::pair<std::string, int>;
        for (MsgItem enabledMsg : enabledMsgs) {
            std::cout << enabledMsg.first << " - received: " << enabledMsg.second << std::endl;
        }
        std::cout << std::endl;
    }

private:

    void processInputData()
    {
        if (!m_inData.empty()) {
            auto consumed = comms::processAllWithDispatch(&m_inData[0], m_inData.size(), m_frame, *this);
            m_inData.erase(m_inData.begin(), m_inData.begin() + consumed);
        }    
    }

    void waitForResponse(ublox::MsgId msg)
    {
        boost::asio::io_service readSvc;
        boost::asio::steady_timer readClock(readSvc);
        std::chrono::microseconds expireTime(USECS_PER_SEC/100);

        if (respondsWithAck(msg)) {
            // wait a second...
            for(int i=0; getAckCheck() && i<100 && m_waitingAckMsg; ++i) {
                readClock.expires_from_now(expireTime);
                readClock.wait();
                performRead();
            }
        }
        else {
            static_cast<void>(&msg);
        }
    }

    void sendMessage(const OutMessage& msg)
    {
        ublox::MsgId msgId = msg.getId();
        OutBuffer buf;
        buf.reserve(m_frame.length(msg)); // Reserve enough space
        auto iter = std::back_inserter(buf);
        for (int i=0; i<3; ++i) {
            auto es = m_frame.write(msg, iter, buf.max_size());
            if (es == comms::ErrorStatus::UpdateRequired) {
                auto* updateIter = &buf[0];
                es = m_frame.update(updateIter, buf.size());
            }
            static_cast<void>(es);
            assert(es == comms::ErrorStatus::Success); // do not expect any error

            if (getAckCheck() && respondsWithAck(msgId)) {
                ackMsgInit(msg.getId());
            }

            // set the ID of the message to be ack/nak'd
            while (!buf.empty()) {
                boost::system::error_code ec;
                auto count = m_pSerial->write_some(boost::asio::buffer(buf), ec);

                if (ec) {
                    std::cerr << "ERROR: write failed with message: " << ec.message() << std::endl;
                    m_pSerial->get_io_service().stop();
                    return;
                }

                buf.erase(buf.begin(), buf.begin() + count);
            }

            // always read even if just to empty the buffer...
            waitForResponse(msgId);

            if (getAckCheck() && respondsWithAck(msgId)) {
                char buf[32];
                memset(buf, 0, 32);
                sprintf(buf, "0x%04X", msgId);
                std::string msgIdStr(buf, strlen(buf));
                if (m_waitingAckMsg) {
                    ackMsgInit(msg.getId());
                    ILOG(("Failed to receive any ACK/NAK message for try ") << i << " for msgId " << msgIdStr);
                    continue;
                }
                else if (!msgAcked) {
                    ILOG(("Received a NAK for try ") << i << " of " << msgIdStr);
                    continue;
                }
                else {
                    ILOG(("Received an ACK for try ") << i << " of " << msgIdStr);
                    break;
                }
            }
            else {
                break;
            }
        }

        if (getAckCheck() && respondsWithAck(msg.getId()) && (m_waitingAckMsg || !msgAcked)) {
            ILOG(("Error: Failed to receive ACK message after three tries..."));
        }
    }

    void ackMsgInit(MsgId msgId)
    {
        m_waitingAckMsg = true;
        m_sentMsgId = msgId;
        msgAcked = false;
    }

    bool respondsWithAck(ublox::MsgId /*msgId*/)
    {
        bool responds = true;
        // switch(msgId) {
        //         responds = false;
        //         break;

        //     default:
        //         break;
        // }

        return responds;
    }

    void disableAllMessages()
    {
        DLOG(("disableAllMessages:"));

        using OutCfgMsgCurrent = ublox::message::CfgMsgCurrent<OutMessage>;
        OutCfgMsgCurrent msg;
        msg.field_rate().value() = 0;

        // Ignore the NMEA messages since they get taken care of in
        // the protocol specification
        //
        // // iterate over a list of all NMEA message IDs
        // DLOG((" disabling NMEA Messages..."));
        // for (NMEA_MsgId msgId : All_NMEA_IDs) {
        //     char buf[32];
        //     memset(buf, 0, 32);
        //     sprintf(buf, "0x%04X", msgId);
        //     std::string msgIdStr(buf, strlen(buf));
        //     DLOG(("Disabling NMEA message ID: ") << msgIdStr);
        //     msg.field_msgId().value() = static_cast<ublox::MsgId>(msgId);
        //     sendMessage(msg);
        // }

        // iterate over a list of all UBX message IDs
        DLOG((" disabling UBX Messages..."));
        for (ublox::MsgId msgId : All_UBX_IDs) {
            char buf[32];
            memset(buf, 0, 32);
            sprintf(buf, "0x%04X", msgId);
            std::string msgIdStr(buf, strlen(buf));
            std::string msgNameStr;
            ILOG(("Session::disableAllMessages(): Disabling UBX message ID: ") << msgIdStr);
            msg.field_msgId().value() = msgId;
            sendMessage(msg);
        }
    }

    void configureUbxProtocol(bool useDetected)
    {
        DLOG(("Session::configureUbxProtocol(): Configuring In/Out Protocol to UBX only for UART..."));
        using OutCfgPrtUart = ublox::message::CfgPrtUart<OutMessage>;

        OutCfgPrtUart msg;
        auto& outProtoMaskField = msg.field_outProtoMask();
        DLOG(("Session::configureUbxProtocol(): default outProtoMaskField: ") << outProtoMaskField.value());

        using OutProtoMaskField = typename std::decay<decltype(outProtoMaskField)>::type;
        outProtoMaskField.setBitValue(OutProtoMaskField::BitIdx_outUbx, 1);
        outProtoMaskField.setBitValue(OutProtoMaskField::BitIdx_outNmea, 0);
        outProtoMaskField.setBitValue(OutProtoMaskField::BitIdx_outRtcm3, 0);

        DLOG(("Session::configureUbxProtocol(): commanded outProtoMaskField: ") << outProtoMaskField.value());

        auto& inProtoMaskField = msg.field_inProtoMask();
        DLOG(("Session::configureUbxProtocol(): default inProtoMaskField: ") << inProtoMaskField.value());

        using InProtoMaskField = typename std::decay<decltype(inProtoMaskField)>::type;
        inProtoMaskField.setBitValue(InProtoMaskField::BitIdx_inUbx, 1);
        inProtoMaskField.setBitValue(InProtoMaskField::BitIdx_inNmea, 0);
        inProtoMaskField.setBitValue(InProtoMaskField::BitIdx_inRtcm, 0);
        inProtoMaskField.setBitValue(InProtoMaskField::BitIdx_inRtcm3, 0);

        DLOG(("Session::configureUbxProtocol(): commanded inProtoMaskField: ") << inProtoMaskField.value());

        auto& baud = msg.field_baudRate().value();
        DLOG(("Session::configureUbxProtocol(): default baud: ") << baud);
        if (useDetected) {
            baud = m_detectedBaudRate;
        }
        else {
            baud = 115200;
        }
        DLOG(("Session::configureUbxProtocol(): commanded baud: ") << baud);

        auto& txReadyField = msg.field_txReady().value();
        auto& txReadyEnabled = std::get<0>(txReadyField);
        DLOG(("Session::configureUbxProtocol(): default txReady enabled: ") 
              << std::string(txReadyEnabled.getBitValue_en() ? "ENABLED" : "DISABLED"));
        txReadyEnabled.setBitValue_en(0);
        DLOG(("Session::configureUbxProtocol(): commanded txReady enabled: ") 
              << std::string(txReadyEnabled.getBitValue_en() ? "ENABLED" : "DISABLED"));

        sendMessage(msg);
    }

    void configureUbxRTCUpdate()
    {
        DLOG(("Session::configureUbxRTCUpdate(): Configuring UBX to update RTC and ephemeris data occasionally..."));
        using OutCfgPm2 = ublox::message::CfgPm2<OutMessage>;
        OutCfgPm2 msg;

        DLOG(("Session::configureUbxRTCUpdate(): default mode: ") << (int)msg.field_flags().field_mode().value());
        msg.field_flags().field_mode().value() = ublox::field::CfgPm2FlagsMembersCommon::ModeVal::Cyclic;
        DLOG(("Session::configureUbxRTCUpdate(): commanded mode: ") << (int)msg.field_flags().field_mode().value());

        DLOG(("Session::configureUbxRTCUpdate(): default message version: ") << (int)msg.field_version().value());
        msg.field_version().value() = 1;
        DLOG(("Session::configureUbxRTCUpdate(): commanded message version: ") << (int)msg.field_version().value());

        DLOG(("Session::configureUbxRTCUpdate(): default max startup duration: ") << (int)msg.field_maxStartupStateDur().value());
        msg.field_maxStartupStateDur().value() = 0; // ublox figures it out
        DLOG(("Session::configureUbxRTCUpdate(): commanded max startup duration: ") << (int)msg.field_maxStartupStateDur().value());
        

        DLOG(("Session::configureUbxRTCUpdate(): default update period: ") << msg.field_updatePeriod().value());
        msg.field_updatePeriod().value() = 1000; // update nav 1000 mS = 1/sec
        DLOG(("Session::configureUbxRTCUpdate(): commanded update period: ") << msg.field_updatePeriod().value());

        DLOG(("Session::configureUbxRTCUpdate(): default search period: ") << msg.field_searchPeriod().value());
        msg.field_searchPeriod().value() = 10; // seconds on
        DLOG(("Session::configureUbxRTCUpdate(): commanded search period: ") << msg.field_searchPeriod().value());

        DLOG(("Session::configureUbxRTCUpdate(): default grid offset: ") << msg.field_gridOffset().value());
        DLOG(("Session::configureUbxRTCUpdate(): default min acq timeout: ") << msg.field_minAcqTime().value());
        DLOG(("Session::configureUbxRTCUpdate(): default on time: ") << msg.field_onTime().value());
        
        auto& outPM2LowFlags = msg.field_flags().field_bitsLow();
        DLOG(("Session::configureUbxRTCUpdate(): default low bits: ") << outPM2LowFlags.value());
        outPM2LowFlags.setBitValue_extintBackup(0);
        outPM2LowFlags.setBitValue_extintSel(0);
        outPM2LowFlags.setBitValue_extintWake(0);
        DLOG(("Session::configureUbxRTCUpdate(): commanded low bits: ") << outPM2LowFlags.value());

        auto& outPM2MidFlags = msg.field_flags().field_bitsMid();
        DLOG(("Session::configureUbxRTCUpdate(): default mid bits: ") << outPM2MidFlags.value());
        outPM2MidFlags.setBitValue_updateRTC(1);
        outPM2MidFlags.setBitValue_updateEPH(1);
        outPM2MidFlags.setBitValue_waitTimeFix(0);
        outPM2MidFlags.setBitValue_doNotEnterOff(0);
        DLOG(("Session::configureUbxRTCUpdate(): commanded mid bits: ") << outPM2MidFlags.value());

        sendMessage(msg);
    }

    void configureUbxPowerMode()
    {
        DLOG(("Session::enableDefaultMessages(): Configuring UBX Power Mode..."));
        using OutCfgRxm = ublox::message::CfgRxm<OutMessage>;

        OutCfgRxm msg;
        auto& outLpMode = msg.field_lpMode().value();

        using OutLpModeValType = typename std::decay<decltype(outLpMode)>::type;
        outLpMode = OutLpModeValType::Continuous;

        sendMessage(msg);
    }

    void configureUbxNavMode()
    {
        DLOG(("Session::enableDefaultMessages(): Configuring UBX NAV Mode..."));
        using OutCfgNav5 = ublox::message::CfgNav5<OutMessage>;
        OutCfgNav5 msg;
        auto& outDynamicModel = msg.field_dynModel().value();

        using OutDynamicModelType = typename std::decay<decltype(outDynamicModel)>::type;
        // using OutDynamicModelType = typename ublox::message::CfgNav5FieldsCommon::DynModelVal;
        outDynamicModel = OutDynamicModelType::Portable;

        auto& utcStd = msg.field_utcStandard().value();
        using UtcStdType = typename std::decay<decltype(utcStd)>::type;
        utcStd = UtcStdType::GPS;

        sendMessage(msg);
    }

    void configGnss()
    {
        DLOG(("Session::configGnss(): Set up which GNSS sat systems are in use, and reserve channels for them."));
        using OutCfgGnss = ublox::message::CfgGnss<OutMessage>;
        OutCfgGnss cfgGnssMsg;

        cfgGnssMsg.field_msgVer().value() = 0;
        cfgGnssMsg.field_numTrkChUse().value() = 0xFF;
        
        cfgGnssMsg.field_numConfigBlocks().value() = 3;
        auto& gnssCfgBlocks = cfgGnssMsg.field_list().value();
        gnssCfgBlocks.resize(cfgGnssMsg.field_numConfigBlocks().value());

        auto& cfgBlock0 = gnssCfgBlocks[0];
        cfgBlock0.field_gnssId().value() = ublox::field::GnssIdVal::GPS;
        auto& enable = cfgBlock0.field_flags().field_bitsLow();
        enable.setBitValue_enable(1);
        auto& sigCfg = cfgBlock0.field_flags().field_sigCfgMask().value();
        sigCfg = 0x01;
        cfgBlock0.field_maxTrkCh().value() = 8;
        cfgBlock0.field_resTrkCh().value() = 16;

        auto& cfgBlock1 = gnssCfgBlocks[1];
        cfgBlock1.field_gnssId().value() = ublox::field::GnssIdVal::QZSS;
        auto& enable1 = cfgBlock1.field_flags().field_bitsLow();
        enable1.setBitValue_enable(1);
        auto& sigCfg1 = cfgBlock1.field_flags().field_sigCfgMask().value();
        sigCfg1 = 0x01;
        cfgBlock1.field_maxTrkCh().value() = 0;
        cfgBlock1.field_resTrkCh().value() = 3;

        auto& cfgBlock2 = gnssCfgBlocks[2];
        cfgBlock2.field_gnssId().value() = ublox::field::GnssIdVal::SBAS;
        auto& enable2 = cfgBlock2.field_flags().field_bitsLow();
        enable2.setBitValue_enable(0);
        auto& sigCfg2 = cfgBlock2.field_flags().field_sigCfgMask().value();
        sigCfg2 = 0x01;
        cfgBlock2.field_maxTrkCh().value() = 4;
        cfgBlock2.field_resTrkCh().value() = 0;

        sendMessage(cfgGnssMsg);
    }

    void configurePPS()
    {
        // first set up the measurement rate
        DLOG(("Session::configurePPS(): Configuring UBX Measurement Rate..."));
        using OutCfgRate = ublox::message::CfgRate<OutMessage>;
        OutCfgRate cfgRatemsg;
        auto& measRate = cfgRatemsg.field_measRate().value();
        measRate = 1000; // 1000 mS = 1S ==> time between GNSS measurements
        auto& navRate = cfgRatemsg.field_navRate().value();
        navRate = 1; // 1 nav measurement per nav solution
        auto& timeRef = cfgRatemsg.field_timeRef().value();
        timeRef = ublox::message::CfgRateFieldsCommon::TimeRefVal::UTC;

        sendMessage(cfgRatemsg);

        // then set up the PPS signal output
        DLOG(("Session::configurePPS(): Configuring UBX PPS Output..."));
        using OutCfgTp5 = ublox::message::CfgTp5<OutMessage>;
        OutCfgTp5 cfgTp5Msg;

        auto& version = cfgTp5Msg.field_version().value();
        version = 0;

        auto& tpIdx = cfgTp5Msg.field_tpIdx().value();
        tpIdx = ublox::field::CfgTp5TpIdxVal::TIMEPULSE;

        auto& bits = cfgTp5Msg.field_flags().field_bits();
        bits.setBitValue_active(1);
        bits.setBitValue_lockGnssFreq(1); // really true?
        bits.setBitValue_polarity(1); // rising edge @ top of second
        bits.setBitValue_alignToTow(1);
        bits.setBitValue_isFreq(0);
        bits.setBitValue_isLength(1);
        bits.setBitValue_lockedOtherSet(1);
    
        auto& timeGrid = cfgTp5Msg.field_flags().field_gridUtcGnss().value();
        timeGrid = ublox::message::CfgTp5Fields<>::FlagsMembers::GridUtcGnssVal::UTC;

        cfgTp5Msg.field_period().value().value() = 1000000; // usec
        cfgTp5Msg.field_pulseLen().value().value() = 0;
        cfgTp5Msg.field_userConfigDelay().value() = 0;
        cfgTp5Msg.field_antCableDelay().value() = 50;       // nsec
        cfgTp5Msg.field_periodLock().value().value() = 1000000;       // usec
        cfgTp5Msg.field_pulseLenLock().value().value() = 100000;    // usec
    
        sendMessage(cfgTp5Msg);
    }

    void enableDefaultMessages()
    {
        DLOG(("Session::enableDefaultMessages(): Enabling NAV PVT Message..."));
        using OutCfgMsg = ublox::message::CfgMsg<OutMessage>;
        OutCfgMsg msg;
        
        msg.field_msgId().value() = ublox::MsgId_NavPvt;

        // only enable it for uart1
        auto& ifaceRateVector = msg.field_rates().value();
        ifaceRateVector.resize((unsigned)ublox::field::CfgPrtPortIdVal::UART + 1);
        ifaceRateVector[(unsigned)ublox::field::CfgPrtPortIdVal::UART].value() = 1; // 1/nav solution delivered
        
        sendMessage(msg);

        std::pair<std::string, int> NavPvt(InNavPvt().doName(), 0);
        enabledMsgs.insert(NavPvt);

        DLOG(("Session::enableDefaultMessages(): Enabling TIM TP Message..."));
        msg.field_msgId().value() = ublox::MsgId_TimTp;
        
        sendMessage(msg);

        std::pair<std::string, int> TimTp(InTimTp().doName(), 0);
        enabledMsgs.insert(TimTp);
    }

    std::auto_ptr<SerialPort> m_pSerial;
    std::auto_ptr<IoService> m_pIoService;
    std::string m_device;
    boost::array<std::uint8_t, 512> m_inputBuf;
    std::vector<std::uint8_t> m_inData;
    Frame m_frame;
    bool m_waitingAckMsg;
    MsgId m_sentMsgId;
    bool msgAcked;
    std::map<std::string, int> enabledMsgs;
    bool checkEnabledMsgs;
    bool ackCheck;
    int m_detectedBaudRate;
};

NidasAppArg AckCheck("-a,--ack-check", "",
        "Enable Ack/Nak checking when sending messages to the u-blox receiver.", "");
NidasAppArg Enable("NOT IMPLEMENTED -E,--enable-msg", "-E UBX-NAV-LLV",
        "Enable a specific u-blox binary message.", "");
NidasAppArg Disable("NOT IMPLEMENTED -D,--disable-msg", "-D UBX-NAV-LLV",
        "Disable a specific u-blox binary message.", "");
NidasAppArg NoBreak("-n,--no-break", "", 
        "Disables the feature to collect enough data to ascertain that the u-blox GPS receiver is operational, "
        "and then exit. This option .", "");
NidasAppArg Device("-d,--device", "i.e. /dev/ttygps?",
        "Serial device to which a u-blox GPS receiver is connected, and which this program uses.", "/dev/gps0");
NidasApp app("ubloxbin");

int usage(const char* argv0)
{
    std::cerr
<< argv0 << " is a utility to control the configuration of a u-blox NEO-M8Q GPS receiver." << std::endl
<< "By default it configures the NEO-M8Q GPS Receiver in the following manner: " << std::endl
<< "   * Disables all NMEA and UBX messages and enables the UBX-NAV-PVT message. " << std::endl
<< "   * Configures all UBX messages to use the onboard UART. " << std::endl
<< "   * Configures the Real Time Clock to be updated when the receiver has a fix. " << std::endl
<< "   * Configures the receiver to always run - i.e. never enter power saving mode. " << std::endl
<< "   * Collects sufficient enabled receiver messages to determine it is configured correctly and exits. " << std::endl
<< std::endl
<< "As described below, there is a command line option to specify the serial port, " << std::endl
<< "command line options to enable/disable UBX messages, and a command line option to " << std::endl
<< "enable a test to make sure the GPS is operational." << std::endl
<< std::endl
<< "Usage: " << argv0 << " [-d <device ID> | -b | -D UBX-NAV-PVT | -E UBX-NAV-POLL | -l <log level>]" << std::endl
<< "       " << argv0 << " -d <device ID> -l <log level>" << std::endl
<< "       " << argv0 << " -d <device ID> -l <log level>" << std::endl
<< "       " << argv0 << " -m -l <log level>" << std::endl << std::endl
<< app.usage();

    return 1;
}

int parseRunString(int argc, char* argv[])
{
    app.enableArguments(AckCheck | Device | Enable | Disable | NoBreak | app.Help | app.loggingArgs());

    ArgVector args = app.parseArgs(argc, argv);
    if (app.helpRequested())
    {
        return usage(argv[0]);
    }
    
    return 0;
}

int main(int argc, char** argv)
{
    if (parseRunString(argc, argv) != 0) {
        return -1;
    }

    try {
        Session session(Device.getValue());
        if (!session.initIoService()) {
            return usage(argv[0]);
        }

        boost::asio::signal_set signals(session.getIoService(), SIGINT, SIGTERM);
        signals.async_wait(
            [&](const boost::system::error_code& ec, int signum)
            {
                session.getIoService().stop();
                if (ec) {
                    std::cerr << "ERROR: " << ec.message() << std::endl;
                    return;
                }

                std::cerr << "Termination due to signal " << signum << std::endl;
            });

        if (!session.initSerial(9600)) {
            return usage(argv[0]);
        }

        if (AckCheck.specified()) {
            DLOG(("ubloxbin: AckCheck is specified."));
            session.setAckCheck();            
        }

        // We start up the asio io_service in a thread so that we can check for Ack/Nak 
        // after sending each configuration message.
        boost::thread run_thread([&] { session.getIoService().run(); });

        if (!AckCheck.specified()) {
            // temporarily turn it on...
            session.setAckCheck();
        }

        // spyOnCapturedInput = true;
        if (!session.findBaudRate()) {
            return usage(argv[0]);
        }
        // spyOnCapturedInput = false;

        if (!AckCheck.specified()) {
            // turn it back off...
            session.setAckCheck(false);
        }

        session.configureUbx();

        session.printEnabledMsgs();

        // Wait for configuration to finish before checking for correct
        // operation
        if (!NoBreak.specified()) {
            session.setCheckEnabledMsgs();
        }

        boost::asio::io_service readSvc;
        boost::asio::steady_timer readClock(readSvc);
        boost::asio::signal_set readSignals(readSvc, SIGINT, SIGTERM);

        while (true) {
            readClock.expires_from_now(std::chrono::seconds(1));
            readClock.wait();
            session.performRead();
            if (!NoBreak.specified() && session.testEnabledMsgs()) 
                break;
        }

        session.getIoService().stop();
        run_thread.join();
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: Unexpected exception: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}

