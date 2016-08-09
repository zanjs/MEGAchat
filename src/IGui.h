#ifndef IGUI_H
#define IGUI_H

namespace karere
{
class ChatRoom;
class GroupChatRoom;

/** The karere chat application GUI interface class, that the app needs to
 * implement. Usually this interface is implemented by the main app window.
 */
class IGui
{
public:
/** Interface that received contact name updates. Each contactlist item
 * implements it, as well as each chat window */
    class ITitleDisplay
    {
    public:
        /** Tells the GUI to update the displayed contact/groupchat name */
        virtual void updateTitle(const std::string& title) = 0;
        /** Tells the GUI to show/remove an unread message counter next to the
         * contact/groupchat name. If count == 0, then the indicator should be
         * removed, if count > 0, the indicator should show the exact value of
         * count, if count < 0, then there are *at least* \c count unread messages,
         * and possibly more. In that case the indicator should show e.g. '2+' */
        virtual void updateOverlayCount(int count) {}
        /** Tells the GUI to update the indicator that shows the online status
         * of the contact/groupchat (online, offline, busy, etc) */
        virtual void updateOnlineIndication(karere::Presence state) = 0;
        /** For group chats, tells the GUI that there has been a change in the
         * group composition. \c updateTitle() will be received as well, so this
         * event is not critical for name display updates
         */
        virtual void onMembersUpdated() {}
    };
    class ICallGui{};
    /** This interface must be implemented by chat windows. It inherits
     * chatd::Listener in order to receive chatd events, and ITitleDisplay,
     * in order to receive chat title/online status events
     */
    class IChatWindow: public chatd::Listener, public ITitleDisplay
    {
    public:
        /** Returns the ICallGui instance associated with that chat window */
        virtual ICallGui* callGui() = 0;
        /** Returns the RTC call event handler associated with that chat window */
        virtual rtcModule::IEventHandler* callEventHandler() = 0;
        /** The app should show the chat window when this is called */
        virtual void show() = 0;
        /** The app should hide the chat window when this is called */
        virtual void hide() = 0;
    };
    /** This is the interafce that must be implemented by the class implementing
     * the login dialog, shown when the app is run the first time and there is no
     * persisted login session */
    class ILoginDialog
    {
    public:
        enum LoginStage { kAuthenticating, kBadCredentials, kLoggingIn, kFetchingNodes, kLoginComplete, kLast=kLoginComplete};
        /** This is the method that karere calls when it needs the dialog shown
         * and credentials entered. It should return the username and password
         * via the returned promise
         */
        virtual promise::Promise<std::pair<std::string, std::string>> requestCredentials() = 0;
        /** Called when the state of the login operation changes,
         * to inform the user about the progress of the login operation.
         */
        virtual void setState(LoginStage state) {}
        virtual ~ILoginDialog() {}
    };
    /** Called when karere needs to create a login dialog */
    virtual ILoginDialog* createLoginDialog() = 0;
    /** Called when karere needs to instantiate a chat window for that 1on1 or
     * group chatroom
     */
    virtual IChatWindow* createChatWindow(karere::ChatRoom& room) = 0;
    /** Implemented by contactlist items, including groupchat items */
    class IContactGui: public ITitleDisplay
    {
    public:
        /** Called when the chat window with that contact/groupchat must be shown */
        virtual void showChatWindow() = 0;
        /** Used only for contacts (not groupchats). Called when the contact's
         * visibility has changed, i.e. the contact was removed or added. The contact
         * itself is never removed from the contactlist, because it is hard-linked
         * with its chatroom, which must exist even if the contact is removed (for
         * viewing the history in read-only mode)
         */
        virtual void onVisibilityChanged(int newVisibility) = 0;
    };
    /** Manages a GUI contactlist implementation */
    class IContactList
    {
    public:
        /** Called when a contact GUI item needs to be added to the GUI contactlist */
        virtual IContactGui* createContactItem(Contact& contact) = 0;
        /** Called when a groupchat GUI item needs to be added to the GUI contactlist */
        virtual IContactGui* createGroupChatItem(GroupChatRoom& room) = 0;
        /** Called when a contact GUI item has to be removed from the GUI contactlist */
        virtual void removeContactItem(IContactGui* item) = 0;
        /** Called when a groupchat GUI item to be removed from the GUI contactlist */
        virtual void removeGroupChatItem(IContactGui* item) = 0;
        /** Must retirn the 1on1 chat window instance for the specified user handle.
         * If one does not exist, it must be created and returned
         */
        virtual IChatWindow& chatWindowForPeer(uint64_t handle) = 0;
    };
    /** Returns the contactlist interface */
    virtual IContactList& contactList() = 0;
    /** Called by karere when our own online state/presence has changed. */
    virtual void onOwnPresence(Presence pres) {} //may include flags
    /** Called when an incoming contact request has been received. To accept
      * or decline the request, the GUI should call
      * \c MegaApi::replyContactRequest with the \c req object
      */
    virtual void onIncomingContactRequest(const mega::MegaContactRequest& req) = 0;
    /** Called by karere when there is an incoming call. The app must create
     * a rtcModule::IEventHandler to handle events related to that incoming
     * call request (such as cancel of the call request).
     * Normally this rtcModule::IEventHandler instance is a class that displays
     * an incoming call GUI, that has a shared pointer to the rtcModule::ICallAnswer
     * object that is used to answer or reject the call.
     */
    virtual rtcModule::IEventHandler*
        createCallAnswerGui(const std::shared_ptr<rtcModule::ICallAnswer>& ans) = 0;
    /** Called by karere when we become participants in a 1on1 or a group chat.
     * This may happen upon startup when the chat list or updates of it are
     * received, after we have become contacts with someone, or when a group chat
     * invite has been accepted by the other party, etc.
     */
    virtual void notifyInvited(const ChatRoom& room) {}
    /** Implements showing of the client GUI */
    virtual void show() = 0;
    /** Returns whether the client GUI is visible */
    virtual bool visible() const = 0;
    /** Called when karere is about to terminate */
    virtual void onTerminate() {}
    virtual ~IGui() {}
};
}

#endif // IGUI

