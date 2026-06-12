/* On-device Wi-Fi provisioning. Brings up an open SoftAP with a captive
 * portal where the user enters their network credentials; on submit the
 * credentials are saved to NVS and the device reboots. Takes over the display
 * with a themed setup screen. Does NOT return (it reboots). */
#pragma once

void provision_run(void);
