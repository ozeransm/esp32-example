
async function getData() {
  try {
    const response = await fetch('./DB/db.jsonl');
    
    if (!response.ok) {
      throw new Error(`Помилка завантаження бази: ${response.status}`);
    }

    const textData = await response.text();
    const recordsArray = textData
      .split(/\r?\n/)                  
      .map(line => line.trim())     
      .filter(line => line !== '')  
      .map(line => JSON.parse(line)); 
    return recordsArray;

  } catch (error) {
    console.error("Не вдалося прочитати NDJSON базу:", error);
    return []; 
  }
}

async function insertData(id='', name='',text='') { 
   try {
    const response = await fetch('./api/insert', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json' 
      },
      body: JSON.stringify({id, name, text}) 
    });

    if (!response.ok) {
      throw new Error(`Сервер не зміг зберегти запис: ${response.status}`);
    }

    const result = await response.text();
    console.log("Успішно збережено на ESP32:", result);

    return {id, name, text};

  } catch (error) {
    console.error("Помилка під час інсерту:", error);
    return null;
  }
}
async function delData(id=0) {
    try {
   const response = await fetch(`./api/delete?id=${id}`, {
      method: 'DELETE', 
    });

    if (!response.ok) {
      throw new Error(`Сервер не зміг видалити запис: ${response.status}`);
    }

    const result = await response.text();
    console.log("Відповідь сервера:", result);
    return true;

  } catch (error) {
    console.error("Помилка під час видалення запису:", error);
    return false;
  }
}
async function updateData(data) {
    try {
    const response = await fetch(`./api/update`, {  
      method: 'PUT',
      headers: {
        'Content-Type': 'application/json' 
      },
      body: JSON.stringify(data) 
    });
    
    if (!response.ok) {
      throw new Error(`Сервер не зміг оновити запис: ${response.status}`);
    }
    const result = await response.text();
    console.log("Успішно оновлено на ESP32:", result);
  } catch (error) {
    console.error("Помилка під час оновлення запису:", error);
  }

}
export {getData,insertData,delData,updateData};