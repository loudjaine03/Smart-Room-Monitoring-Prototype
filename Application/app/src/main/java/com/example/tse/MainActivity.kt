package com.example.tse

import android.annotation.SuppressLint
import android.graphics.Color
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity() {

    private lateinit var tvPeopleCount: TextView
    private lateinit var tvAvailableSeats: TextView
    private lateinit var tvTemperature: TextView
    private lateinit var tvHumidity: TextView
    private lateinit var tvHeating: TextView
    private lateinit var tvDate: TextView

    private val handler = Handler(Looper.getMainLooper())

    // Real phone with USB + adb reverse tcp:5000 tcp:5000
    private val SERVER_URL = "http://127.0.0.1:5000/latest"

    private val ROOM_CAPACITY = 20

    // Fetch Arduino/database data every 3 seconds
    private val fetchRunnable = object : Runnable {
        override fun run() {
            fetchData()
            handler.postDelayed(this, 3000)
        }
    }

    // Update real date/time every second
    private val clockRunnable = object : Runnable {
        override fun run() {
            updateDateTime()
            handler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        initViews()
    }

    override fun onResume() {
        super.onResume()
        handler.post(fetchRunnable)
        handler.post(clockRunnable)
    }

    override fun onPause() {
        super.onPause()
        handler.removeCallbacks(fetchRunnable)
        handler.removeCallbacks(clockRunnable)
    }

    private fun initViews() {
        tvPeopleCount = findViewById(R.id.textView1)
        tvAvailableSeats = findViewById(R.id.textView3)
        tvTemperature = findViewById(R.id.textView4)
        tvHumidity = findViewById(R.id.textViewHumidityValue)
        tvHeating = findViewById(R.id.textView9)
        tvDate = findViewById(R.id.dateText)

        tvPeopleCount.text = "--"
        tvAvailableSeats.text = "--"
        tvTemperature.text = "--"
        tvHumidity.text = "--"
        tvHeating.text = "--"

        updateDateTime()
    }

    private fun updateDateTime() {
        val currentDateTime = SimpleDateFormat(
            "dd/MM/yyyy HH:mm:ss",
            Locale.getDefault()
        ).format(Date())

        tvDate.text = currentDateTime
    }

    private fun fetchData() {
        Thread {
            try {
                val url = URL(SERVER_URL)
                val connection = url.openConnection() as HttpURLConnection

                connection.requestMethod = "GET"
                connection.connectTimeout = 3000
                connection.readTimeout = 3000

                if (connection.responseCode == 200) {
                    val response = connection.inputStream.bufferedReader().readText()
                    updateUI(JSONObject(response))
                } else {
                    showToast("Server error: ${connection.responseCode}")
                }

                connection.disconnect()

            } catch (e: Exception) {
                showToast("Connection failed: ${e.message}")
            }
        }.start()
    }

    @SuppressLint("SetTextI18n")
    private fun updateUI(json: JSONObject) {
        val peopleCount = json.optInt("people_count", 0)

        val temperature = if (json.isNull("temperature")) {
            0.0
        } else {
            json.optDouble("temperature", 0.0)
        }

        val humidity = if (json.isNull("humidity")) {
            0.0
        } else {
            json.optDouble("humidity", 0.0)
        }

        val heaterState = json.optString("heater_state", "OFF")
        val availableSeats = maxOf(0, ROOM_CAPACITY - peopleCount)

        handler.post {
            tvPeopleCount.text = peopleCount.toString()
            tvAvailableSeats.text = availableSeats.toString()

            // Temperature value only, XML already has °C
            tvTemperature.text = String.format(Locale.getDefault(), "%.1f", temperature)

            // Humidity value only, XML already has %
            tvHumidity.text = String.format(Locale.getDefault(), "%.0f", humidity)

            tvHeating.text = if (heaterState == "ON") {
                "ON"
            } else {
                "OFF"
            }

            tvAvailableSeats.setTextColor(
                when {
                    availableSeats <= 0 -> Color.RED
                    availableSeats <= 5 -> Color.parseColor("#FFA500")
                    else -> Color.GREEN
                }
            )

            tvHeating.setTextColor(
                if (heaterState == "ON") Color.RED else Color.BLUE
            )
        }
    }

    private fun showToast(msg: String) {
        handler.post {
            Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
        }
    }
}