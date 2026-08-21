import React, { useState } from 'react';
import { Monitor, Snowflake, User, Mail } from 'lucide-react';
import './Settings.css';

export default function Settings() {
  const [isFrozen, setIsFrozen] = useState(true);

  return (
    <div className="settings-page">
      <div className="page-header">
        <h1>Настройки аккаунта</h1>
      </div>

      <div className="settings-section">
        <div className="settings-grid">
          {/* HWID Block */}
          <div className="glass-card settings-card">
            <div className="card-header">
              <Monitor size={20} className="header-icon" />
              <h3>Привязка HWID</h3>
            </div>
            <p className="card-subtitle">Ваш HWID | Сброс доступен раз в 15 часов</p>
            <input type="text" className="input-field" disabled value="-" />
          </div>

          {/* Freeze Block */}
          <div className="glass-card settings-card">
            <div className="card-header">
              <Snowflake size={20} className="header-icon" />
              <h3>Заморозка подписок</h3>
            </div>
            <p className="card-subtitle">Заморозка доступна раз в 3 дня</p>
            {isFrozen && (
              <div className="freeze-alert">
                Подписки заморожены
              </div>
            )}
            <button 
              className="btn-secondary w-full"
              onClick={() => setIsFrozen(!isFrozen)}
            >
              {isFrozen ? 'Разморозить подписки' : 'Заморозить подписки'}
            </button>
          </div>
        </div>
      </div>

      <div className="page-header mt-8">
        <h2>Безопасность</h2>
      </div>

      <div className="settings-section">
        <div className="settings-grid">
          {/* Password Block */}
          <div className="glass-card settings-card">
            <div className="card-header">
              <User size={20} className="header-icon" />
              <h3>Изменение пароля</h3>
            </div>
            <p className="card-subtitle">Повысьте безопасность выбрав более надежный пароль</p>
            
            <div className="form-group">
              <input type="password" className="input-field" placeholder="Текущий пароль" />
              <input type="password" className="input-field" placeholder="Новый пароль" />
              <input type="password" className="input-field" placeholder="Новый пароль ещё раз" />
              <button className="btn-secondary w-full mt-4">Сохранить</button>
            </div>
          </div>

          {/* Email Block */}
          <div className="glass-card settings-card">
            <div className="card-header">
              <Mail size={20} className="header-icon" />
              <h3>Изменение почты</h3>
            </div>
            <p className="card-subtitle">Этот адрес электронной почты привязан к вашей учетной записи</p>
            
            <div className="form-group email-group">
              <input type="text" className="input-field" disabled value="fraud*****@xyecoc.com" />
              <button className="btn-secondary w-full">Изменить эл. почту</button>
            </div>
          </div>
        </div>
      </div>
    </div>
  );
}
