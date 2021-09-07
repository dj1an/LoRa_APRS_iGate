#include <logger.h>

#include <TimeLib.h>

#include "Task.h"
#include "TaskRouter.h"
#include "project_configuration.h"

String create_lat_aprs(double lat);
String create_long_aprs(double lng);

RouterTask::RouterTask(TaskQueue<std::shared_ptr<APRSMessage>> &fromModem, TaskQueue<std::shared_ptr<APRSMessage>> &toModem, TaskQueue<std::shared_ptr<APRSMessage>> &toAprsIs) : Task(TASK_ROUTER, TaskRouter), _fromModem(fromModem), _toModem(toModem), _toAprsIs(toAprsIs) {
}

RouterTask::~RouterTask() {
}

bool RouterTask::setup(System &system) {
  // setup beacon
  _beacon_timer.setTimeout(system.getUserConfig()->beacon.timeout * 60 * 1000);

  _beaconMsg = std::shared_ptr<APRSMessage>(new APRSMessage());
  _beaconMsg->setSource(system.getUserConfig()->callsign);
  _beaconMsg->setDestination("APLG01");
  String lat = create_lat_aprs(system.getUserConfig()->beacon.positionLatitude);
  String lng = create_long_aprs(system.getUserConfig()->beacon.positionLongitude);
  _beaconMsg->getBody()->setData(String("=") + lat + "L" + lng + "&" + system.getUserConfig()->beacon.message);

  return true;
}

bool RouterTask::loop(System &system) {
  // do routing
  if (!_fromModem.empty()) {
    std::shared_ptr<APRSMessage> modemMsg = _fromModem.getElement();

    if (system.getUserConfig()->aprs_is.active && modemMsg->getSource() != system.getUserConfig()->callsign) {
      std::shared_ptr<APRSMessage> aprsIsMsg = std::make_shared<APRSMessage>(*modemMsg);
      String                       path      = aprsIsMsg->getPath();

      if (!(path.indexOf("RFONLY") != -1 || path.indexOf("NOGATE") != -1 || path.indexOf("TCPIP") != -1)) {
        if (!path.isEmpty()) {
          path += ",";
        }

        aprsIsMsg->setPath(path + "qAR," + system.getUserConfig()->callsign);

        logPrintD("APRS-IS: ");
        logPrintlnD(aprsIsMsg->toString());
        _toAprsIs.addElement(aprsIsMsg);
      } else {
        logPrintlnD("APRS-IS: no forward => RFonly");
      }
    } else {
      if (!system.getUserConfig()->aprs_is.active)
        logPrintlnD("APRS-IS: disabled");

      if (modemMsg->getSource() == system.getUserConfig()->callsign)
        logPrintlnD("APRS-IS: no forward => own packet received");
    }

    if (system.getUserConfig()->digi.active && modemMsg->getSource() != system.getUserConfig()->callsign) {
      std::shared_ptr<APRSMessage> digiMsg    = std::make_shared<APRSMessage>(*modemMsg);
      String                       path       = digiMsg->getPath();
      String                       dest       = digiMsg->getDestination();
      bool                         doDigipeat = false;

      // wide1-1 and DST-1 are exclusive. Only one allowed
      if (path.indexOf("WIDE1-1") >= 0 && path.indexOf(system.getUserConfig()->callsign) == -1 && dest.indexOf("-") == -1) {
        // fixme
        logPrintD("WIDE Digipeating: ");
        doDigipeat = true;
      } else {
        // Destination digipeating
        if (dest.indexOf("-") >= 0 && path.indexOf(system.getUserConfig()->callsign) == -1) {
          int destSsidIdx = 0;
          // for future use? at now, we digipeat only once.
          // int                          destSsid = 0;
          destSsidIdx = dest.indexOf("-");
          // destSsid = dest.substring(destSsidIdx+1).toInt();
          // destSsid -= 1;
          dest.remove(destSsidIdx, 2);

          // if (! destSsid == 0){
          //  dest += "-" + (String(destSsid));
          //}

          logPrintD("DST Digipeating: ");
          digiMsg->setDestination(dest);
          doDigipeat = true;
        } else {
          // No wide1-1 no DST-1. Digipeat in case of Cross-Frequency Digipeating?
          // should never be used - highly experimental :)
          // logPrintlnD("Illegal Digipeating : ");
          doDigipeat = false;
        }
      }

      if (doDigipeat) {
        digiMsg->setPath(system.getUserConfig()->callsign + "*");
        logPrintlnD(digiMsg->toString());
        _toModem.addElement(digiMsg);
      }
    }
  }

  // check for beacon
  if (_beacon_timer.check()) {
    logPrintD("[" + timeString() + "] ");
    logPrintlnD(_beaconMsg->encode());

    if (system.getUserConfig()->aprs_is.active)
      _toAprsIs.addElement(_beaconMsg);

    if (system.getUserConfig()->digi.beacon) {
      _toModem.addElement(_beaconMsg);
    }

    system.getDisplay().addFrame(std::shared_ptr<DisplayFrame>(new TextFrame("BEACON", _beaconMsg->toString())));

    _beacon_timer.start();
  }

  uint32_t diff = _beacon_timer.getTriggerTimeInSec();
  _stateInfo    = "beacon " + String(uint32_t(diff / 60)) + ":" + String(uint32_t(diff % 60));

  return true;
}
