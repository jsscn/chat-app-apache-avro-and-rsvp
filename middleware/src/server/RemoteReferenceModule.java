package server;
import java.util.HashMap;
import java.util.Map;

/*
 * We will first create the RemoteReferenceModule class on the server side. 
 * Its most important function is to keep track of the server objects 
 * that have been registered in the platform.
*/

public class RemoteReferenceModule {
	//remote object reference table
	Map<String, Object> remoteObjects = new HashMap<String, Object>();
	//remoteObjects.put( 	remote reference, 	local reference 	)
	
	//table of local proxies
	//MOET NOG NIET

	public void register(Object remoteObject) {
		remoteObjects.put(remoteObject.getClass().getSimpleName(), remoteObject);
	}
	
	public Object retrieve(String remoteObjectName) {
		return remoteObjects.get(remoteObjectName);
	}
}