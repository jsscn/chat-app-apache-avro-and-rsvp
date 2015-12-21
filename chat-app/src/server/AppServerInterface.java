/**
 * Autogenerated by Avro
 * 
 * DO NOT EDIT DIRECTLY
 */
package server;

@SuppressWarnings("all")
@org.apache.avro.specific.AvroGenerated
public interface AppServerInterface {
  public static final org.apache.avro.Protocol PROTOCOL = org.apache.avro.Protocol.parse("{\"protocol\":\"AppServerInterface\",\"namespace\":\"server\",\"types\":[{\"type\":\"enum\",\"name\":\"ClientStatus\",\"symbols\":[\"LOBBY\",\"PUBLIC\",\"PRIVATE\"]}],\"messages\":{\"echo\":{\"request\":[{\"name\":\"message\",\"type\":\"int\"}],\"response\":\"int\"},\"registerClient\":{\"request\":[{\"name\":\"username\",\"type\":\"string\"},{\"name\":\"ipaddress\",\"type\":\"string\"},{\"name\":\"port\",\"type\":\"int\"}],\"response\":\"int\"},\"isNameAvailable\":{\"request\":[{\"name\":\"username\",\"type\":\"string\"}],\"response\":\"boolean\"},\"unregisterClient\":{\"request\":[{\"name\":\"username\",\"type\":\"string\"}],\"response\":\"int\"},\"getListOfClients\":{\"request\":[],\"response\":\"string\"},\"setClientState\":{\"request\":[{\"name\":\"username\",\"type\":\"string\"},{\"name\":\"state\",\"type\":\"ClientStatus\"}],\"response\":\"int\"},\"sendMessage\":{\"request\":[{\"name\":\"username\",\"type\":\"string\"},{\"name\":\"message\",\"type\":\"string\"}],\"response\":\"int\"},\"sendRequest\":{\"request\":[{\"name\":\"username1\",\"type\":\"string\"},{\"name\":\"username2\",\"type\":\"string\"}],\"response\":\"int\"},\"cancelRequest\":{\"request\":[{\"name\":\"username1\",\"type\":\"string\"},{\"name\":\"username2\",\"type\":\"string\"}],\"response\":\"int\"},\"removeRequest\":{\"request\":[{\"name\":\"username1\",\"type\":\"string\"},{\"name\":\"username2\",\"type\":\"string\"}],\"response\":\"int\"},\"requestResponse\":{\"request\":[{\"name\":\"username1\",\"type\":\"string\"},{\"name\":\"username2\",\"type\":\"string\"},{\"name\":\"responseBool\",\"type\":\"boolean\"}],\"response\":\"int\"},\"getMyRequests\":{\"request\":[{\"name\":\"username\",\"type\":\"string\"}],\"response\":\"string\"}}}");
  int echo(int message) throws org.apache.avro.AvroRemoteException;
  int registerClient(java.lang.CharSequence username, java.lang.CharSequence ipaddress, int port) throws org.apache.avro.AvroRemoteException;
  boolean isNameAvailable(java.lang.CharSequence username) throws org.apache.avro.AvroRemoteException;
  int unregisterClient(java.lang.CharSequence username) throws org.apache.avro.AvroRemoteException;
  java.lang.CharSequence getListOfClients() throws org.apache.avro.AvroRemoteException;
  int setClientState(java.lang.CharSequence username, server.ClientStatus state) throws org.apache.avro.AvroRemoteException;
  int sendMessage(java.lang.CharSequence username, java.lang.CharSequence message) throws org.apache.avro.AvroRemoteException;
  int sendRequest(java.lang.CharSequence username1, java.lang.CharSequence username2) throws org.apache.avro.AvroRemoteException;
  int cancelRequest(java.lang.CharSequence username1, java.lang.CharSequence username2) throws org.apache.avro.AvroRemoteException;
  int removeRequest(java.lang.CharSequence username1, java.lang.CharSequence username2) throws org.apache.avro.AvroRemoteException;
  int requestResponse(java.lang.CharSequence username1, java.lang.CharSequence username2, boolean responseBool) throws org.apache.avro.AvroRemoteException;
  java.lang.CharSequence getMyRequests(java.lang.CharSequence username) throws org.apache.avro.AvroRemoteException;

  @SuppressWarnings("all")
  public interface Callback extends AppServerInterface {
    public static final org.apache.avro.Protocol PROTOCOL = server.AppServerInterface.PROTOCOL;
    void echo(int message, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
    void registerClient(java.lang.CharSequence username, java.lang.CharSequence ipaddress, int port, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
    void isNameAvailable(java.lang.CharSequence username, org.apache.avro.ipc.Callback<java.lang.Boolean> callback) throws java.io.IOException;
    void unregisterClient(java.lang.CharSequence username, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
    void getListOfClients(org.apache.avro.ipc.Callback<java.lang.CharSequence> callback) throws java.io.IOException;
    void setClientState(java.lang.CharSequence username, server.ClientStatus state, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
    void sendMessage(java.lang.CharSequence username, java.lang.CharSequence message, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
    void sendRequest(java.lang.CharSequence username1, java.lang.CharSequence username2, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
    void cancelRequest(java.lang.CharSequence username1, java.lang.CharSequence username2, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
    void removeRequest(java.lang.CharSequence username1, java.lang.CharSequence username2, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
    void requestResponse(java.lang.CharSequence username1, java.lang.CharSequence username2, boolean responseBool, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
    void getMyRequests(java.lang.CharSequence username, org.apache.avro.ipc.Callback<java.lang.CharSequence> callback) throws java.io.IOException;
  }
}