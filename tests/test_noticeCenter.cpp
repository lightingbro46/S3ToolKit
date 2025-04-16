#include <csignal>
#include "Util/util.h"
#include "Util/logger.h"
#include "Util/NoticeCenter.h"
using namespace std;
using namespace toolkit;

// Broadcast Name 1
#define NOTICE_NAME1 "NOTICE_NAME1"
// Broadcast Name 2
#define NOTICE_NAME2 "NOTICE_NAME2"

// Program Exit Flag
bool g_bExitFlag = false;


int main() {
    // Set Program Exit Signal Handler
    signal(SIGINT, [](int){g_bExitFlag = true;});
    // Set Log
    Logger::Instance().add(std::make_shared<ConsoleChannel>());

    // Add a Listener to the Event NOTICE_NAME1
    // The First Parameter of the addListener Method is a Tag, Used to Delete the Listener
    // Note that the Number and Type of Parameters in the Listener Callback Must be Exactly the Same as Those in the emitEvent Broadcast, Otherwise Unpredictable Errors May Occur
    NoticeCenter::Instance().addListener(0,NOTICE_NAME1,
            [](int &a,const char * &b,double &c,string &d){
        DebugL << a << " " << b << " " << c << " " << d;
        NoticeCenter::Instance().delListener(0,NOTICE_NAME1);

        NoticeCenter::Instance().addListener(0,NOTICE_NAME1,
                                             [](int &a,const char * &b,double &c,string &d){
                                                 InfoL << a << " " << b << " " << c << " " << d;
                                             });
    });

    // Listen for the NOTICE_NAME2 Event
    NoticeCenter::Instance().addListener(0,NOTICE_NAME2,
            [](string &d,double &c,const char *&b,int &a){
        DebugL << a << " " << b << " " << c << " " << d;
        NoticeCenter::Instance().delListener(0,NOTICE_NAME2);

        NoticeCenter::Instance().addListener(0,NOTICE_NAME2,
                                             [](string &d,double &c,const char *&b,int &a){
                                                 WarnL << a << " " << b << " " << c << " " << d;
                                             });

    });
    int a = 0;
    while(!g_bExitFlag){
        const char *b = "b";
        double c = 3.14;
        string d("d");
        // Broadcast the Event Every 1 Second, If the Parameter Type is Uncertain, a Forced Conversion Can be Added
        NoticeCenter::Instance().emitEvent(NOTICE_NAME1,++a,(const char *)"b",c,d);
        NoticeCenter::Instance().emitEvent(NOTICE_NAME2,d,c,b,a);
        sleep(1); // sleep 1 second
    }
    return 0;
}
