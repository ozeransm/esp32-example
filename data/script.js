console.log("Script loaded");

function toggleLed(action) {
      fetch('/' + action)
        .then(response => response.text())
        .then(text => {
          document.getElementById('status').innerText = text;
        });
    }