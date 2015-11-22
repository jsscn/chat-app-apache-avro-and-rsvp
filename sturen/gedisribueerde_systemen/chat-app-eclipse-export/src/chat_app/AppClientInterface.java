/**
 * Autogenerated by Avro
 * 
 * DO NOT EDIT DIRECTLY
 */
package chat_app;

@SuppressWarnings("all")
@org.apache.avro.specific.AvroGenerated
public interface AppClientInterface {
  public static final org.apache.avro.Protocol PROTOCOL = org.apache.avro.Protocol.parse("{\"protocol\":\"AppClientInterface\",\"namespace\":\"chat_app\",\"types\":[],\"messages\":{\"receiveMessage\":{\"request\":[{\"name\":\"message\",\"type\":\"string\"}],\"response\":\"int\"}}}");
  int receiveMessage(java.lang.CharSequence message) throws org.apache.avro.AvroRemoteException;

  @SuppressWarnings("all")
  public interface Callback extends AppClientInterface {
    public static final org.apache.avro.Protocol PROTOCOL = chat_app.AppClientInterface.PROTOCOL;
    void receiveMessage(java.lang.CharSequence message, org.apache.avro.ipc.Callback<java.lang.Integer> callback) throws java.io.IOException;
  }
}