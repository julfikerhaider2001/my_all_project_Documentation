package xyz.borgo.farm

import android.app.Application
import com.google.firebase.FirebaseApp

class BorgoFarmApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        FirebaseApp.initializeApp(this)
    }
}
