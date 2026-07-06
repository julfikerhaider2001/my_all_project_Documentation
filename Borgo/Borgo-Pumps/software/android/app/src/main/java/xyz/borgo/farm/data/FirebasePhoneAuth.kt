package xyz.borgo.farm.data

import android.app.Activity
import android.util.Log
import com.google.firebase.FirebaseException
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.auth.PhoneAuthCredential
import com.google.firebase.auth.PhoneAuthOptions
import com.google.firebase.auth.PhoneAuthProvider
import java.util.concurrent.TimeUnit

class FirebasePhoneAuth(
    private val auth: FirebaseAuth = FirebaseAuth.getInstance()
) {
    fun requestCode(
        activity: Activity,
        phoneNumber: String,
        onCodeSent: (verificationId: String) -> Unit,
        onVerified: () -> Unit,
        onError: (String) -> Unit
    ) {
        val options = PhoneAuthOptions.newBuilder(auth)
            .setPhoneNumber(phoneNumber)
            .setTimeout(60L, TimeUnit.SECONDS)
            .setActivity(activity)
            .setCallbacks(object : PhoneAuthProvider.OnVerificationStateChangedCallbacks() {
                override fun onVerificationCompleted(credential: PhoneAuthCredential) {
                    Log.i("BorgoFirebaseAuth", "Phone verification completed automatically")
                    auth.signInWithCredential(credential)
                        .addOnSuccessListener { onVerified() }
                        .addOnFailureListener {
                            Log.e("BorgoFirebaseAuth", "Auto sign-in failed", it)
                            onError(it.message ?: "Verification failed")
                        }
                }

                override fun onVerificationFailed(exception: FirebaseException) {
                    Log.e("BorgoFirebaseAuth", "Phone verification failed", exception)
                    onError(exception.message ?: "Could not send verification code")
                }

                override fun onCodeSent(
                    verificationId: String,
                    token: PhoneAuthProvider.ForceResendingToken
                ) {
                    Log.i("BorgoFirebaseAuth", "Verification code sent")
                    onCodeSent(verificationId)
                }
            })
            .build()
        Log.i("BorgoFirebaseAuth", "Requesting phone verification for $phoneNumber")
        PhoneAuthProvider.verifyPhoneNumber(options)
    }

    fun verifyCode(
        verificationId: String,
        code: String,
        onVerified: () -> Unit,
        onError: (String) -> Unit
    ) {
        val credential = PhoneAuthProvider.getCredential(verificationId, code)
        auth.signInWithCredential(credential)
            .addOnSuccessListener { onVerified() }
            .addOnFailureListener {
                Log.e("BorgoFirebaseAuth", "Manual code sign-in failed", it)
                onError(it.message ?: "Invalid verification code")
            }
    }
}
