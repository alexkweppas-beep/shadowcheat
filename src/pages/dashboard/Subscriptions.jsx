import React, { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { LayoutGrid } from 'lucide-react';
import './Subscriptions.css';

export default function Subscriptions() {
  const navigate = useNavigate();
  const [activationCode, setActivationCode] = useState('');

  return (
    <div className="subscriptions-page">
      <div className="page-header">
        <h1>Информация о подписках</h1>
        <button className="btn-secondary" onClick={() => navigate('/games/external')}>
          + Продукты
        </button>
      </div>

      <div className="subscriptions-grid">
        <div className="glass-card activate-card">
          <h3 className="card-title">АКТИВИРОВАТЬ</h3>
          <div className="activate-form">
            <input 
              type="text" 
              className="input-field" 
              placeholder="Введите код активации"
              value={activationCode}
              onChange={(e) => setActivationCode(e.target.value)}
            />
            <button className="btn-primary">Активировать</button>
          </div>
        </div>

        <div className="glass-card empty-subscriptions-card">
          <div className="empty-icon">
            <LayoutGrid size={24} />
          </div>
          <h2>Подписок пока нет</h2>
          <p>Откройте каталог, выберите продукт и оформите подписку. После оплаты она появится здесь вместе с файлами и инструкциями.</p>
          <button className="btn-secondary" onClick={() => navigate('/games/external')}>
            Перейти в каталог
          </button>
        </div>
      </div>
    </div>
  );
}
