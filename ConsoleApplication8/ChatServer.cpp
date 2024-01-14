#include "pch.h"
using namespace System;
using namespace System::Data::SqlClient;
using namespace System::Net;
using namespace System::Net::Sockets;
using namespace System::Text;
using namespace System::Collections::Generic;
ref class Server {
private:
    TcpListener^ tcpListener;
    TcpClient^ tcpClient;
    // Метод для открытия подключения к базе данных
    SqlConnection^ OpenDatabaseConnection() {
        String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";
        SqlConnection^ connection = gcnew SqlConnection(connectionString);

        try {
            connection->Open();
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error opening database connection: " + ex->Message);
            // Обработка ошибки при открытии подключения к базе данных
        }

        return connection;
    }
    //Функция для отправки ответа клиенту
    void SendResponse(NetworkStream^ networkStream, String^ response) {
        array<Byte>^ responseBytes = Encoding::UTF8->GetBytes(response);
        networkStream->Write(responseBytes, 0, responseBytes->Length);
    }
    // Функция для обработки запроса на отправку файла
    void HandleSendFileRequest(NetworkStream^ networkStream, String^ chatName, String^ username, array<Byte>^ fileData, String^ fileName) {
        Console::WriteLine("Started processing file send request for user '{0}' and file '{1}'.", username, fileName);

        int userId = GetUserIdByUsername(username);
        if (userId == -1) {
            SendResponse(networkStream, "User not found");
            return;
        }
        // Открываем подключение к базе данных с помощью нового метода
        SqlConnection^ connection = OpenDatabaseConnection();

        if (connection != nullptr) {
            try {
                String^ query = "INSERT INTO Messages (ChatName, UserID, FileName, FileData, MessageText, Timestamp, FileSize) VALUES (@chatName, @userId, @fileName, @fileData, 'file', @timestamp, @fileSize)";
                SqlCommand^ command = gcnew SqlCommand(query, connection);
                command->Parameters->AddWithValue("@chatName", chatName);
                command->Parameters->AddWithValue("@userId", userId);
                command->Parameters->AddWithValue("@fileName", fileName);

                // Используйте VARBINARY(MAX) для хранения бинарных данных файла
                SqlParameter^ fileDataParam = gcnew SqlParameter("@fileData", System::Data::SqlDbType::VarBinary, -1);
                fileDataParam->Value = fileData;
                command->Parameters->Add(fileDataParam);

                command->Parameters->AddWithValue("@timestamp", DateTime::Now);

                // Добавляем размер файла в параметры команды
                int fileSize = fileData->Length;
                command->Parameters->AddWithValue("@fileSize", fileSize);

                command->ExecuteNonQuery();

                // Отправьте размер файла клиенту перед отправкой файла
                String^ fileInfo = String::Format("{0}:{1}", fileName, fileSize);
                array<Byte>^ fileInfoBytes = Encoding::UTF8->GetBytes(fileInfo);
                networkStream->Write(fileInfoBytes, 0, fileInfoBytes->Length);

                // Отправка файла частями
                const int bufferSize = 1024; // Размер буфера
                for (int i = 0; i < fileSize; i += bufferSize) {
                    int chunkSize = Math::Min(bufferSize, fileSize - i);
                    networkStream->Write(fileData, i, chunkSize);
                }

                // Добавьте информацию о размере файла в логи
                Console::WriteLine("File '{0}' sent successfully. Size: {1} bytes", fileName, fileSize);

                SendResponse(networkStream, "File sent successfully");
            }
            catch (Exception^ ex) {
                Console::WriteLine("Error: " + ex->Message);
                SendResponse(networkStream, "Error sending file");
            }
            finally {
                // Закрываем подключение к базе данных
                connection->Close();
                Console::WriteLine("Completed processing file send request for user '{0}' and file '{1}'.", username, fileName);
            }
        }
        else {
            // Если не удалось установить соединение с базой данных, отправляем клиенту сообщение об ошибке
            SendResponse(networkStream, "Error connecting to the database");
        }
    }

    // Функция для получения файла из базы данных
    bool GetFileFromDatabase(String^ chatName, String^ fileName, NetworkStream^ networkStream) {
        String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";

        try {
            SqlConnection^ connection = gcnew SqlConnection(connectionString);
            connection->Open();

            String^ query = "SELECT FileData FROM Messages WHERE ChatName = @chatName AND FileName = @fileName";
            SqlCommand^ command = gcnew SqlCommand(query, connection);
            command->Parameters->AddWithValue("@chatName", chatName);
            command->Parameters->AddWithValue("@fileName", fileName);

            SqlDataReader^ reader = command->ExecuteReader();

            if (reader->Read()) {
                array<Byte>^ fileData = (array<Byte>^)reader["FileData"];
                int fileSize = fileData->Length;

                // Проверка размера файла перед отправкой (не более 20 МБ)
                if (fileSize <= 20 * 1024 * 1024) { // 20 МБ в байтах
                    // Отправить клиенту имя файла и размер файла
                    String^ fileInfo = String::Format("{0}:{1}", fileName, fileSize);
                    array<Byte>^ fileInfoBytes = Encoding::UTF8->GetBytes(fileInfo);
                    networkStream->Write(fileInfoBytes, 0, fileInfoBytes->Length);

                    // Отправка всего файла сразу
                    networkStream->Write(fileData, 0, fileSize);
                    return true;
                }
                else {
                    Console::WriteLine("File size exceeds the limit (20 MB).");
                    SendResponse(networkStream, "File size exceeds the limit (20 MB)");
                }
            }
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error: " + ex->Message);
            SendResponse(networkStream, "Error receiving file");
        }

        return false;
    }

    // Функция для обработки запроса на получение файла
    void HandleReceiveFileRequest(NetworkStream^ networkStream, String^ fileName, String^ chatName) {
        Console::WriteLine("Started processing file receive request for chat.", chatName);

        if (GetFileFromDatabase(chatName, fileName, networkStream)) {
            Console::WriteLine("File '{0}' sent successfully.", fileName);
        }

        Console::WriteLine("Completed processing file receive request for chat '{0}'.", chatName);
    }


    // Обработка запроса на получение сообщений из чата
    // Функция для форматирования сообщения о файле
    String^ FormatFileMessage(String^ username, DateTime timestamp, int fileSize, String^ fileName, String^ currentUser) {
        String^ messageDisplay = (username == currentUser) ? "You" : username;
        String^ formattedTime = timestamp.ToString("yyyy-MM-dd HH:mm:ss");
        return String::Format("file:{0} ({1}) [{2} bytes]: {3}\n", messageDisplay, formattedTime, fileSize, fileName);
    }

    // Функция для форматирования текстового сообщения
    String^ FormatTextMessage(String^ username, DateTime timestamp, String^ messageText, String^ currentUser) {
        String^ messageDisplay = (username == currentUser) ? "You" : username;
        String^ formattedTime = timestamp.ToString("yyyy-MM-dd HH:mm:ss");
        return String::Format("message:{0} ({1}): {2}\n", messageDisplay, formattedTime, messageText);
    }

    // Функция для обработки запроса на получение сообщений из чата
    void HandleGetChatMessagesRequest(NetworkStream^ networkStream, String^ chatName, String^ currentUser) {
        String^ connectionString = "Data Source=(localdb)\\MSSQLLocalDB;Initial Catalog=chat;Integrated Security=True;";
        try {
            SqlConnection^ connection = gcnew SqlConnection(connectionString);
            connection->Open();

            // Получаем идентификатор текущего пользователя
            int currentUserId = GetUserIdByUsername(currentUser);

            // SQL-запрос для получения текстовых сообщений и информации о файлах из чата
            String^ query = "SELECT u.Username, m.Timestamp, m.MessageText, m.FileName, m.FileSize FROM Messages m INNER JOIN Users u ON m.UserID = u.UserID WHERE m.ChatName = @chatName ORDER BY m.Timestamp";
            SqlCommand^ command = gcnew SqlCommand(query, connection);
            command->Parameters->AddWithValue("@chatName", chatName);

            SqlDataReader^ reader = command->ExecuteReader();

            StringBuilder^ messagesBuilder = gcnew StringBuilder();
            while (reader->Read()) {
                String^ username = reader->GetString(0);
                DateTime timestamp = reader->GetDateTime(1);
                String^ messageText = reader->GetString(2);
                String^ fileName = reader->IsDBNull(3) ? "" : reader->GetString(3);
                int fileSize = reader->IsDBNull(4) ? 0 : reader->GetInt32(4); // Получение размера файла

                // Формирование строки сообщения
                String^ displayMessage;
                if (!String::IsNullOrEmpty(fileName)) {
                    displayMessage = FormatFileMessage(username, timestamp, fileSize, fileName, currentUser);
                }
                else {
                    displayMessage = FormatTextMessage(username, timestamp, messageText, currentUser);
                }

                messagesBuilder->Append(displayMessage);
            }

            // Логирование сообщений и информации о файлах перед отправкой списка сообщений
            Console::WriteLine(messagesBuilder->ToString());

            // Отправка списка сообщений клиенту
            SendResponse(networkStream, messagesBuilder->ToString());

            reader->Close();
            connection->Close();
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error: " + ex->Message);

            // Отправка сообщения об ошибке клиенту
            String^ errorMessage = "chat_messages_response:Error retrieving chat messages";
            SendResponse(networkStream, errorMessage);
        }
    }






    // Функция для получения UserID по имени пользователя из базы данных
    int GetUserIdByUsername(String^ username) {
        int userId = -1;  // Инициализируем значение -1, чтобы указать, что пользователя не найдено

        try {
            SqlConnection^ connection = OpenDatabaseConnection(); // Используем метод для установления соединения
            // Запрос для выбора UserID по имени пользователя
            String^ query = "SELECT UserID FROM Users WHERE Username = @username";
            SqlCommand^ command = gcnew SqlCommand(query, connection);
            command->Parameters->AddWithValue("@username", username);

            // Выполнение запроса
            Object^ result = command->ExecuteScalar();

            // Проверка, было ли получено значение
            if (result != nullptr) {
                userId = Convert::ToInt32(result);
            }

            connection->Close();
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error: " + ex->Message);
            // Обработка ошибки при подключении к базе данных или выполнении запроса
        }

        return userId;
    }



    // Обработка запроса на отправку сообщения в чат
    // Функция для проверки существования чата
    bool ChatExists(String^ chatName) {
        SqlConnection^ connection = OpenDatabaseConnection();
        if (connection == nullptr) {
            return false;
        }

        try {
            String^ query = "SELECT COUNT(*) FROM Chats WHERE ChatName = @chatName";
            SqlCommand^ command = gcnew SqlCommand(query, connection);
            command->Parameters->AddWithValue("@chatName", chatName);

            int count = Convert::ToInt32(command->ExecuteScalar());

            return (count > 0);
        }
        catch (Exception^ ex) {
            Console::WriteLine("Error: " + ex->Message);
            return false;
        }
        finally {
            connection->Close();
        }
    }

    // Функция для обработки запроса на отправку сообщения в чат
    void HandleSendMessageRequest(NetworkStream^ networkStream, String^ requestData) {
        // Разбиваем запрос на части с помощью разделителя ":"
        array<String^>^ messageData = requestData->Split(':');

        // Проверяем, соответствует ли формат запроса ожидаемому формату
        if (messageData->Length == 4 && messageData[0] == "send_message_request") {
            String^ chatName = messageData[1];
            String^ sender = messageData[2];
            String^ messageText = messageData[3];
            DateTime timestamp = DateTime::Now;

            if (!ChatExists(chatName)) {
                // Если чат не существует, отправляем клиенту сообщение об ошибке
                String^ response = "Chat not found";
                SendResponse(networkStream, response);
                Console::WriteLine("Chat not found");
                return;
            }

            SqlConnection^ connection = OpenDatabaseConnection();
            if (connection == nullptr) {
                // Ошибка при открытии подключения, отправляем клиенту сообщение об ошибке
                String^ response = "Error opening database connection";
                SendResponse(networkStream, response);
                Console::WriteLine("Error opening database connection");
                return;
            }

            try {
                // Получаем идентификатор пользователя по его имени
                int userId = GetUserIdByUsername(sender);

                if (userId != -1) {
                    // Если пользователь существует, выполняем вставку сообщения в базу данных
                    String^ insertMessageQuery = "INSERT INTO Messages (ChatName, UserID, MessageText, Timestamp) VALUES (@chatName, @UserID, @messageText, @timestamp)";
                    SqlCommand^ insertMessageCommand = gcnew SqlCommand(insertMessageQuery, connection);
                    insertMessageCommand->Parameters->AddWithValue("@chatName", chatName);
                    insertMessageCommand->Parameters->AddWithValue("@UserID", userId);
                    insertMessageCommand->Parameters->AddWithValue("@messageText", messageText);
                    insertMessageCommand->Parameters->AddWithValue("@timestamp", timestamp);

                    insertMessageCommand->ExecuteNonQuery();

                    // Отправляем клиенту сообщение об успешной отправке
                    String^ response = "Message sent successfully";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Message sent successfully");
                }
                else {
                    // Если пользователь не найден, отправляем клиенту сообщение об ошибке
                    String^ response = "Sender not found";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Sender not found");
                }
            }
            catch (Exception^ ex) {
                // Обрабатываем и логируем ошибку при выполнении запроса
                Console::WriteLine("Error: " + ex->Message);
                String^ response = "Error sending message";
                SendResponse(networkStream, response);
            }
            finally {
                connection->Close();
            }
        }
        else {
            // Если формат запроса неверный, отправляем клиенту сообщение об ошибке
            String^ response = "Invalid send message request format";
            SendResponse(networkStream, response);
            Console::WriteLine("Invalid send message request format");
        }
    }

    // Обработка запроса на получение списка чатов по ключевому слову
    void HandleGetChatListRequest(NetworkStream^ networkStream, String^ value) {
        // Переменная для хранения списка чатов
        String^ chatList;

        // Подключение к базе данных
        SqlConnection^ connection = OpenDatabaseConnection();

        if (connection != nullptr) {
            try {
                // Запрос к базе данных для выборки чатов, содержащих указанное ключевое слово
                String^ query = "SELECT ChatName FROM Chats WHERE ChatName LIKE '%' + @value + '%'";
                SqlCommand^ command = gcnew SqlCommand(query, connection);
                command->Parameters->AddWithValue("@value", value);

                // Выполнение запроса и чтение результатов
                SqlDataReader^ reader = command->ExecuteReader();

                // Создание списка для хранения имен чатов
                List<String^> chatNames;
                while (reader->Read()) {
                    chatNames.Add(reader->GetString(0));
                }

                // Преобразование списка в массив строк и объединение его в одну строку с разделителем ","
                array<Object^>^ chatNamesArray = chatNames.ToArray();
                chatList = String::Join(",", chatNamesArray);

                // Закрытие соединения с базой данных
                reader->Close();
            }
            catch (Exception^ ex) {
                // Обработка ошибки при выполнении запроса
                Console::WriteLine("Error: " + ex->Message);

                // Устанавливаем строку chatList с сообщением об ошибке
                chatList = "chat_list_response:Error retrieving chat list";
            }

            // Закрытие соединения с базой данных
            connection->Close();
        }
        else {
            // Обработка ошибки при открытии подключения
            chatList = "chat_list_response:Error opening database connection";
        }

        // Отправка списка чатов на клиент
        SendResponse(networkStream, chatList);
        Console::WriteLine("Chat list sent to client");
    }

    // Обработка запроса на создание нового чата
    void HandleCreateChatRequest(NetworkStream^ networkStream, String^ requestData) {
        // Разбиваем данные запроса на части по символу ":"
        array<String^>^ createChatData = requestData->Split(':');

        // Проверяем, что получен корректный формат запроса
        if (createChatData->Length == 2 && createChatData[0] == "create_chat_request") {
            // Извлекаем имя нового чата из данных запроса
            String^ newChatName = createChatData[1];

            // Подключение к базе данных
            SqlConnection^ connection = OpenDatabaseConnection();

            if (connection != nullptr) {
                try {
                    // Проверяем, существует ли чат с таким именем
                    String^ checkChatQuery = "SELECT COUNT(*) FROM Chats WHERE ChatName = @ChatName";
                    SqlCommand^ checkChatCommand = gcnew SqlCommand(checkChatQuery, connection);
                    checkChatCommand->Parameters->AddWithValue("@ChatName", newChatName);

                    // Выполняем запрос и получаем количество существующих чатов с таким именем
                    int existingChatCount = (int)checkChatCommand->ExecuteScalar();

                    if (existingChatCount > 0) {
                        // Если чат с таким именем уже существует, отправляем клиенту сообщение об ошибке
                        String^ response = "Chat with this name already exists";
                        SendResponse(networkStream, response);
                        Console::WriteLine("Chat with this name already exists");
                    }
                    else {
                        // Создаем новый чат
                        String^ createChatQuery = "INSERT INTO Chats (ChatName) VALUES (@ChatName)";
                        SqlCommand^ createChatCommand = gcnew SqlCommand(createChatQuery, connection);
                        createChatCommand->Parameters->AddWithValue("@ChatName", newChatName);

                        // Выполняем запрос на создание чата
                        createChatCommand->ExecuteNonQuery();

                        // Отправляем клиенту сообщение об успешном создании чата
                        String^ response = "Chat created successfully";
                        SendResponse(networkStream, response);
                        Console::WriteLine("Chat created successfully");
                    }

                    // Закрываем соединение с базой данных
                    connection->Close();
                }
                catch (Exception^ ex) {
                    // Обрабатываем ошибку, если что-то пошло не так при создании чата
                    Console::WriteLine("Error: " + ex->Message);
                    String^ response = "Error during chat creation";
                    SendResponse(networkStream, response);
                }
            }
            else {
                // Если произошла ошибка при открытии подключения, отправляем клиенту сообщение об ошибке
                String^ response = "Error opening database connection";
                SendResponse(networkStream, response);
                Console::WriteLine("Error opening database connection");
            }
        }
        else {
            // Если формат запроса некорректный, отправляем клиенту сообщение об ошибке
            String^ response = "Invalid create chat request format";
            SendResponse(networkStream, response);
            Console::WriteLine("Invalid create chat request format");
        }
    }

    void HandleLoginRequest(NetworkStream^ networkStream, String^ requestData) {
        // Разбиваем данные запроса на части по символу ":"
        array<String^>^ loginData = requestData->Split(':');

        // Проверяем, что получен корректный формат запроса
        if (loginData->Length == 3 && loginData[0] == "login_request") {
            // Извлекаем имя пользователя и пароль из данных запроса
            String^ username = loginData[1];
            String^ password = loginData[2];

            // Подключение к базе данных
            SqlConnection^ connection = OpenDatabaseConnection();

            if (connection != nullptr) {
                try {
                    // Запрос для проверки существования пользователя с указанным именем и паролем
                    String^ query = "SELECT COUNT(*) FROM Users WHERE Username = @username AND Password = @password";
                    SqlCommand^ command = gcnew SqlCommand(query, connection);
                    command->Parameters->AddWithValue("@username", username);
                    command->Parameters->AddWithValue("@password", password);

                    // Выполняем запрос и получаем результат (количество совпадений)
                    int result = (int)command->ExecuteScalar();

                    // Закрываем соединение с базой данных
                    connection->Close();

                    if (result > 0) {
                        // Если совпадения найдены, отправляем клиенту сообщение об успешном входе
                        String^ response = "Login successful";
                        SendResponse(networkStream, response);
                        Console::WriteLine("Login successful");
                    }
                    else {
                        // Если совпадения не найдены, отправляем клиенту сообщение об ошибке входа
                        String^ response = "Login failed";
                        SendResponse(networkStream, response);
                        Console::WriteLine("Login failed");
                    }
                }
                catch (Exception^ ex) {
                    // Обрабатываем ошибку, если что-то пошло не так при проверке входа
                    Console::WriteLine("Error: " + ex->Message);
                    String^ response = "Error during login";
                    SendResponse(networkStream, response);
                }
            }
            else {
                // Если произошла ошибка при открытии подключения, отправляем клиенту сообщение об ошибке
                String^ response = "Error opening database connection";
                SendResponse(networkStream, response);
                Console::WriteLine("Error opening database connection");
            }
        }
        else {
            // Если формат запроса некорректный, отправляем клиенту сообщение об ошибке
            String^ response = "Invalid login request format";
            SendResponse(networkStream, response);
        }
    }

    // Обработка запроса на регистрацию нового пользователя
    void HandleRegistrationRequest(NetworkStream^ networkStream, String^ requestData) {
        // Разбиваем данные запроса на части по символу ":"
        array<String^>^ registrationData = requestData->Split(':');

        // Проверяем, что получен корректный формат запроса
        if (registrationData->Length == 3 && registrationData[0] == "register_request") {
            // Извлекаем имя пользователя и пароль из данных запроса
            String^ username = registrationData[1];
            String^ password = registrationData[2];

            // Подключение к базе данных
            SqlConnection^ connection = OpenDatabaseConnection();

            if (connection != nullptr) {
                try {
                    // Запрос для проверки существования пользователя с указанным именем
                    String^ checkUserQuery = "SELECT COUNT(*) FROM Users WHERE Username = @username";
                    SqlCommand^ checkUserCommand = gcnew SqlCommand(checkUserQuery, connection);
                    checkUserCommand->Parameters->AddWithValue("@username", username);

                    // Выполняем запрос и получаем результат (количество совпадений)
                    int existingUserCount = (int)checkUserCommand->ExecuteScalar();

                    if (existingUserCount > 0) {
                        // Если пользователь с таким именем уже существует, отправляем клиенту сообщение об ошибке
                        String^ response = "Username already exists";
                        SendResponse(networkStream, response);
                        Console::WriteLine("Username already exists");
                    }
                    else {
                        // Если пользователь с таким именем не существует, выполняем регистрацию
                        String^ registerQuery = "INSERT INTO Users (Username, Password) VALUES (@username, @password)";
                        SqlCommand^ registerCommand = gcnew SqlCommand(registerQuery, connection);
                        registerCommand->Parameters->AddWithValue("@username", username);
                        registerCommand->Parameters->AddWithValue("@password", password);

                        registerCommand->ExecuteNonQuery();

                        // Отправляем клиенту сообщение об успешной регистрации
                        String^ response = "Registration successful";
                        SendResponse(networkStream, response);
                        Console::WriteLine("Registration successful");
                    }

                    // Закрываем соединение с базой данных
                    connection->Close();
                }
                catch (Exception^ ex) {
                    // Обрабатываем ошибку, если что-то пошло не так при регистрации
                    Console::WriteLine("Error: " + ex->Message);
                    String^ response = "Error during registration";
                    SendResponse(networkStream, response);
                }
            }
            else {
                // Если произошла ошибка при открытии подключения, отправляем клиенту сообщение об ошибке
                String^ response = "Error opening database connection";
                SendResponse(networkStream, response);
                Console::WriteLine("Error opening database connection");
            }
        }
        else {
            // Если формат запроса некорректный, отправляем клиенту сообщение об ошибке
            String^ response = "Invalid registration request format";
            SendResponse(networkStream, response);
            Console::WriteLine("Invalid registration request format");
        }
    }
public:
    // Конструктор класса сервера, запускающий сервер и обрабатывающий запросы от клиентов
    Server() {
        // Создаем объект TcpListener, привязываем его к локальному IP-адресу и порту 1234, и запускаем прослушивание
        tcpListener = gcnew TcpListener(IPAddress::Parse("127.0.0.1"), 1234);
        tcpListener->Start();

        // Выводим сообщение о начале прослушивания
        Console::WriteLine("Server is listening on port 1234...");

        while (true) {
            // Принимаем подключение от клиента
            tcpClient = tcpListener->AcceptTcpClient();
            Console::WriteLine("Client connected.");

            // Получаем поток данных для обмена информацией с клиентом
            NetworkStream^ networkStream = tcpClient->GetStream();
            array<Byte>^ bytes = gcnew array<Byte>(1024);
            int bytesRead = networkStream->Read(bytes, 0, bytes->Length);

            // Преобразуем полученные байты в строку
            String^ requestData = Encoding::UTF8->GetString(bytes, 0, bytesRead);
            Console::WriteLine("Received: {0}", requestData);

            // Определяем тип запроса и вызываем соответствующую функцию обработки
            if (requestData->StartsWith("login_request")) {
                HandleLoginRequest(networkStream, requestData);
            }
            else if (requestData->StartsWith("register_request")) {
                HandleRegistrationRequest(networkStream, requestData);
            }
            else if (requestData->StartsWith("create_chat_request")) {
                HandleCreateChatRequest(networkStream, requestData);
            }
            else if (requestData->StartsWith("get_chat_list_request")) {
                // Получите имя пользователя из запроса и обработайте запрос на список чатов
                array<String^>^ requestParts = requestData->Split(':');
                if (requestParts->Length == 2) {
                    String^ value = requestParts[1];
                    HandleGetChatListRequest(networkStream, value);
                }
                else {
                    String^ response = "Invalid get chat list request format";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Invalid get chat list request format");
                }
            }
            else if (requestData->StartsWith("send_message_request")) {
                HandleSendMessageRequest(networkStream, requestData);
            }
            else if (requestData->StartsWith("get_chat_history_request:")) {
                array<String^>^ requestParts = requestData->Split(':');
                if (requestParts->Length == 3) {
                    String^ chatName = requestParts[1];
                    String^ currentUser = requestParts[2];
                    HandleGetChatMessagesRequest(networkStream, chatName, currentUser);
                }
                else {
                    String^ response = "Invalid get chat history request format";
                    SendResponse(networkStream, response);
                    Console::WriteLine("Invalid get chat history request format");
                }
            }
            else if (requestData->StartsWith("send_file_request")) {
                // Обработка запроса на отправку файла
                array<String^>^ parts = requestData->Split(':');
                if (parts->Length >= 4) {
                    String^ chatName = parts[1];
                    String^ username = parts[2];
                    String^ fileName = parts[3];
                    // Остаток данных считывается как содержимое файла
                    array<Byte>^ fileData = gcnew array<Byte>(bytesRead - (chatName->Length + username->Length + fileName->Length + 4));
                    Array::Copy(bytes, chatName->Length + username->Length + fileName->Length + 4, fileData, 0, fileData->Length);
                    HandleSendFileRequest(networkStream, chatName, username, fileData, fileName);
                }
                else {
                    SendResponse(networkStream, "Invalid send file request format");
                }
            }
            else if (requestData->StartsWith("get_file_request")) {
                // Обработка запроса на получение файла
                array<String^>^ parts = requestData->Split(':');
                if (parts->Length == 3) {
                    String^ fileName = parts[1];
                    String^ chatName = parts[2];
                    HandleReceiveFileRequest(networkStream, fileName, chatName);
                }
                else {
                    SendResponse(networkStream, "Invalid get file request format");
                }
            }
            else {
                // Если запрос имеет некорректный формат, отправляем клиенту сообщение об ошибке
                String^ response = "Invalid request format";
                SendResponse(networkStream, response);
                Console::WriteLine("Invalid request format");
            }

            // Закрываем соединение с клиентом
            tcpClient->Close();
        }
    }
};

int main() {
    Server^ server = gcnew Server();
    return 0;
}
