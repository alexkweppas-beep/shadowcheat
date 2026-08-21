import React, { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import Header from '../components/Header';
import { API_BASE } from '../api.js';
import './dashboard/DashboardPage.css'; // Reusing dashboard styles

export default function DistributorPage() {
  const navigate = useNavigate();
  const [user, setUser] = useState(null);
  const [keys, setKeys] = useState([]);

  useEffect(() => {
    const token = localStorage.getItem('token');
    if (!token) {
      navigate('/');
      return;
    }

    // Check if user is distributor
    fetch(`${API_BASE}/api/user/me`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
      .then(res => res.json())
      .then(data => {
        if (data.error) {
          navigate('/');
        } else if (!data.is_distributor) {
          navigate('/home'); // Redirect to dashboard if not distributor
        } else {
          setUser(data);
          fetchKeys(token);
        }
      })
      .catch(console.error);
  }, [navigate]);

  const fetchKeys = (token) => {
    fetch(`${API_BASE}/api/distributor/keys`, {
      headers: { 'Authorization': `Bearer ${token}` }
    })
      .then(res => res.json())
      .then(data => {
        if (data.ok) setKeys(data.keys);
      })
      .catch(console.error);
  };

  const activeKeys = keys.filter(k => !k.used);
  const usedKeys = keys.filter(k => k.used);

  if (!user) return <div style={{ color: 'white', padding: '40px', textAlign: 'center' }}>Загрузка...</div>;

  return (
    <div className="dashboard-page">
      <Header />

      <div className="container dashboard-container">
        <h2 style={{ color: 'white', marginBottom: '20px', fontSize: '24px' }}>Кабинет Дистрибьютора</h2>

        <div className="dash-row dash-row-2" style={{ marginBottom: '24px' }}>
          <div className="dash-card info-card" style={{ flex: 1 }}>
            <div className="info-label">Всего выдано ключей</div>
            <div className="info-val">{keys.length}</div>
          </div>
          <div className="dash-card info-card" style={{ flex: 1 }}>
            <div className="info-label">Неиспользованных</div>
            <div className="info-val" style={{ color: '#4ade80' }}>{activeKeys.length}</div>
          </div>
          <div className="dash-card info-card" style={{ flex: 1 }}>
            <div className="info-label">Активировано</div>
            <div className="info-val" style={{ color: '#ef4444' }}>{usedKeys.length}</div>
          </div>
        </div>

        <div className="dash-card" style={{ padding: '24px' }}>
          <h3 style={{ color: 'white', marginBottom: '16px' }}>Ваши ключи</h3>

          <div style={{ overflowX: 'auto' }}>
            <table style={{ width: '100%', borderCollapse: 'collapse', color: 'white', textAlign: 'left' }}>
              <thead>
                <tr style={{ borderBottom: '1px solid rgba(255,255,255,0.1)' }}>
                  <th style={{ padding: '12px 8px', color: 'rgba(255,255,255,0.5)', fontWeight: '500' }}>Ключ</th>
                  <th style={{ padding: '12px 8px', color: 'rgba(255,255,255,0.5)', fontWeight: '500' }}>Продукт</th>
                  <th style={{ padding: '12px 8px', color: 'rgba(255,255,255,0.5)', fontWeight: '500' }}>Дней</th>
                  <th style={{ padding: '12px 8px', color: 'rgba(255,255,255,0.5)', fontWeight: '500' }}>Статус</th>
                  <th style={{ padding: '12px 8px', color: 'rgba(255,255,255,0.5)', fontWeight: '500' }}>Дата выдачи</th>
                </tr>
              </thead>
              <tbody>
                {keys.length > 0 ? (
                  keys.map((k, i) => (
                    <tr key={i} style={{ borderBottom: '1px solid rgba(255,255,255,0.05)' }}>
                      <td style={{ padding: '12px 8px', fontFamily: 'monospace', color: '#60a5fa' }}>
                        {k.key_str}
                        <button
                          onClick={() => { navigator.clipboard.writeText(k.key_str); alert('Ключ скопирован!'); }}
                          style={{ marginLeft: '8px', padding: '2px 6px', fontSize: '11px', background: 'rgba(255,255,255,0.1)', border: 'none', borderRadius: '4px', color: 'white', cursor: 'pointer' }}
                        >
                          📋
                        </button>
                      </td>
                      <td style={{ padding: '12px 8px', textTransform: 'uppercase' }}>{k.product_id}</td>
                      <td style={{ padding: '12px 8px' }}>{k.days} дн.</td>
                      <td style={{ padding: '12px 8px' }}>
                        {k.used ? (
                          <span style={{ color: '#ef4444', background: 'rgba(239, 68, 68, 0.1)', padding: '4px 8px', borderRadius: '4px', fontSize: '12px' }}>Активирован</span>
                        ) : (
                          <span style={{ color: '#4ade80', background: 'rgba(74, 222, 128, 0.1)', padding: '4px 8px', borderRadius: '4px', fontSize: '12px' }}>Свободен</span>
                        )}
                      </td>
                      <td style={{ padding: '12px 8px', fontSize: '14px', color: 'rgba(255,255,255,0.7)' }}>
                        {new Date(k.created_at).toLocaleString('ru-RU')}
                      </td>
                    </tr>
                  ))
                ) : (
                  <tr>
                    <td colSpan="5" style={{ padding: '24px', textAlign: 'center', color: 'rgba(255,255,255,0.5)' }}>У вас пока нет ключей. Обратитесь к администратору.</td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </div>

      </div>
    </div>
  );
}
